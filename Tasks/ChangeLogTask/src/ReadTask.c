#include "TaskHandler.h"
#include "ReadTask.h"
#include "ChangeLog.h"
#include "ChangeLogTask.h"
#include "CheckPointTask.h"
#include "ThreadManager.h"
#include "MyTime.h"
#include "DiskLog.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_READ
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_READ

typedef enum FlushState_e {
    FLUSH_IDLE,
    FLUSH_RUNNING,
    FLUSH_SUCCESS,
    FLUSH_ERROR,
}FlushState;

typedef struct ReadTaskBitmap_t {
    uint32 done : 1;//thread done 
}ReadTaskBitmap;

typedef struct ReadTaskContext_t {
    ReadTaskState state;
    volatile ReadTaskBitmap Bitmap;
    ReadTaskCmd cmd;
    ThreadPoolSlot* thReadBLog;
    ThreadPoolSlot* thReadFLog;
    ThreadPoolSlot* thReadLog;
    ThreadPoolSlot* thReadStorage;
    int resp;
}ReadTaskContext;

static uint64 start = 0;
static uint64 us = 0;
static TaskDoneCallBack gl_parentCallback = NULL;
static void* gl_curContext = NULL;

static int parentCallback() {
    if (gl_parentCallback) {
        return gl_parentCallback();
    }
    return -1;
}
static int callback() {
    if (!addTaskToList(TASK_ID_READ, gl_curContext)) {
        ASSERT(TRUE, "read task callback fail\n");
        return -1;
    }
    return 0;
}
static void thReadFromLog(void* in, void* out) {
    int err = 0;
    ReadTaskContext* con = in;
    con->resp = ChangeLogTryReadFromLog(con->cmd.fileId, con->cmd.seek, con->cmd.size, con->cmd.data, con->cmd.offset);
    con->Bitmap.done = 1;
    return;
}
static void thReadFromStorage(void* in, void* out) {
    int err = 0;
    ReadTaskContext* con = in;
    con->resp = StorageRead(con->cmd.fileId, con->cmd.seek, con->cmd.size, con->cmd.data, con->cmd.offset);
    con->Bitmap.done = 1;
    return;
}
static void thReadFromBLog(void* in, void* out) {
    int err = 0;
    ReadTaskContext* con = in;
    ASSERT(con->Bitmap.done, "con->Bitmap.done is already true\n");
    con->resp = ChangeLogReadFromBLog(con->cmd.fileId, con->cmd.seek, con->cmd.size, con->cmd.data, con->cmd.offset);
    con->Bitmap.done = 1;
    return;
}
static void thReadFromFLog(void* in, void* out) {
    int err = 0;
    ReadTaskContext* con = in;
    ASSERT(con->Bitmap.done, "con->Bitmap.done is already true\n");
    con->resp = ChangeLogReadFromFLog(con->cmd.fileId, con->cmd.seek, con->cmd.size, con->cmd.data, con->cmd.offset);
    con->Bitmap.done = 1;
    return;
}


static int init() {
    
    return 0;
}

int initReadTask() {
    return init();
}
//处理客户端的请求
TaskState ReadTask(void* inP) {
    int err = 0;
    BufferListInfo* tmp = inP;
    ASSERT(!tmp, "null context buffer\n");
    ReadTaskContext* con = tmp->buff;
    gl_curContext = tmp;
    ReadTaskState* state = &(con->state);
   
    switch (*state) {
    case READ_TASK_IDLE: {
        con->Bitmap.done = 0;
        con->thReadLog = NULL;
        con->thReadStorage = NULL;

        if (!ChangeLogIsDestIdInLog(con->cmd.fileId)) {
            *state = READ_TASK_READ_FROM_STORAGE;
            return TASK_STATE_RESTART;
        }
    }
    case READ_TASK_READ_FROM_BLOG: {
        *state = READ_TASK_READ_FROM_BLOG;
        ChangeLogSetBLogReadLock();//ZBH bug??
        ThreadParam param = {
            .func = thReadFromBLog,
            .input = con,
        };
        con->thReadBLog = ThreadRun(param);
        if (con->thReadBLog) {
            *state = READ_TASK_WAIT_READ_FROM_BLOG_DONE;
            ASSERT(!ChangeLogIsReadingBLog(), "not set 1 after 1\n");
        }
        else {
            ChangeLogSetBLogReadUnLock();
        }
        return TASK_STATE_RESTART;
    }
    case READ_TASK_WAIT_READ_FROM_BLOG_DONE: {
        *state = READ_TASK_WAIT_READ_FROM_BLOG_DONE;
        if (con->Bitmap.done) {
            con->Bitmap.done = 0;
            ChangeLogSetBLogReadUnLock();
            err = con->resp;
            if (err == 0) {
                //readed
                *state = READ_TASK_COMPLETE;
            }
            else if (err == 1) {
                //need flush
                if (CheckPointTaskIsIdle()) {
                    *state = READ_TASK_CHECKPOINT;
                }
                else {
                    *state = READ_TASK_WAIT_CHECKPOINT_DONE;
                }
            }
            else {
                //not found
                *state = READ_TASK_READ_FROM_FLOG;
            }
            
        }
        else {
            ASSERT(!ChangeLogIsReadingBLog(), "ZBH 1\n");
        }
        return TASK_STATE_RESTART;
    }
    case READ_TASK_READ_FROM_FLOG: {
        ThreadParam param = {
            .func = thReadFromFLog,
            .input = con,
        };
        con->thReadFLog = ThreadRun(param);
        if (con->thReadFLog) {
            *state = READ_TASK_WAIT_READ_FROM_FLOG_DONE;
        }
        return TASK_STATE_RESTART;
    }
    case READ_TASK_WAIT_READ_FROM_FLOG_DONE: {
        *state = READ_TASK_WAIT_READ_FROM_FLOG_DONE;
        if (con->Bitmap.done) {
            con->Bitmap.done = 0;
            err = con->resp;
            if (err == 0) {
                //readed
                *state = READ_TASK_COMPLETE;
            }
            else if (err == 1) {
                //need flush
                if (!CheckPointTaskIsIdle()) {
                    //need wait last checkpoint done
                    return TASK_STATE_RESTART;
                }
                err = ChangeLogSwap();
                ASSERT(err, "swap fail");//BLog not empty
                if (CheckPointTaskIsIdle()) {
                    *state = READ_TASK_CHECKPOINT;
                }
                else {
                    *state = READ_TASK_WAIT_CHECKPOINT_DONE;
                }
            }
            else {
                //not found
                *state = READ_TASK_READ_FROM_STORAGE;
            }
        }
        return TASK_STATE_RESTART;
    }
    case READ_TASK_CHECKPOINT: {
        if (CheckPointTaskIsIdle()) {
            CheckPointTaskParam param = {
                .callback = NULL,
                .resp = NULL
            };
            if (!sendMsgToTask(TASK_ID_CHECK_POINT, &param)) {
                return TASK_STATE_RESTART;
            }
        }
        *state = READ_TASK_WAIT_CHECKPOINT_DONE;
        return TASK_STATE_RESTART;
    }
    case READ_TASK_WAIT_CHECKPOINT_DONE: {
        *state = READ_TASK_WAIT_CHECKPOINT_DONE;
        if (CheckPointTaskIsIdle()) {
            *state = READ_TASK_READ_FROM_BLOG;
        }
        return TASK_STATE_RESTART;
    }
    case READ_TASK_READ_FROM_STORAGE: {
        *state = READ_TASK_READ_FROM_STORAGE;
        ThreadParam param = {
               .func = thReadFromStorage,
               .input = con
        };
        con->thReadStorage = ThreadRun(param);
        if (con->thReadStorage) {
            *state = READ_TASK_WAIT_READ_FROM_STORAGE_DONE;
        }
        return TASK_STATE_RESTART;
    }
    case READ_TASK_WAIT_READ_FROM_STORAGE_DONE: {
        //check response
        if (con->Bitmap.done) {
            *con->cmd.resp = con->resp;
            con->Bitmap.done = 0;
            *state = READ_TASK_COMPLETE;
        }
        return TASK_STATE_RESTART;
    }
    case READ_TASK_COMPLETE: {
        gl_parentCallback = con->cmd.callback;
        freeBuffer(&tmp);
        *state = READ_TASK_IDLE;
        return TASK_STATE_COMPLETE;
    }
    case READ_TASK_ERROR: {
        ASSERT(TRUE, "Read Task Error\n");
        freeBuffer(&tmp);
        *state = READ_TASK_IDLE;
        return TASK_STATE_COMPLETE;
    }
    default:
        ASSERT(TRUE, "");
        break;
    }
    ASSERT(TRUE, "");
    //never hear
    return TASK_STATE_IDLE;
}

bool ReadTaskMessage(void* parent) {
    int err = 0;
    ReadTaskCmd* cmd = parent;
    if (!cmd->data || cmd->size == 0 || cmd->resp == NULL ||!TaskCanAddSuccess(TASK_ID_READ)) {
        ASSERT(TRUE, "Read task invalid input\n");
        return FALSE;
    }
    if ((*cmd->data)) {
        ASSERT(TRUE, "Read task invalid input 2\n");
        return FALSE;
    }
    BufferListInfo* tmp = getBuffer(sizeof(ReadTaskContext), BUFFER_POOL); if (!tmp) { return FALSE; }
    ReadTaskContext* con = tmp->buff;
    memcpy(&con->cmd, cmd, sizeof(ReadTaskCmd));
    con->state = READ_TASK_IDLE;
    err = addTaskToList(TASK_ID_READ, tmp);

    return err;
}
int ReadTaskCallBack() {
    return parentCallback();
}