#include "TaskHandler.h"
#include "ChangeLogTask.h"
#include "CheckPointTask.h"
#include "ThreadManager.h"
#include "MyTime.h"
#include "DiskLog.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_CHANGE_LOG
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_CHANGE_LOG

typedef enum FlushState_e {
    FLUSH_IDLE,
    FLUSH_RUNNING,
    FLUSH_SUCCESS,
    FLUSH_ERROR,
}FlushState;

typedef struct ChangeLogTaskBitmap_t {
    uint32 flushState : 2; //FlushState
    uint32 checkpointState : 2;//CheckPointState
}ChangeLogTaskBitmap;

typedef struct ChangeLogTaskContext_t {
    ChangeLogTaskState state;
    volatile ChangeLogTaskBitmap Bitmap;
    LogCommandItem cmd;
    ThreadPoolSlot* thFlush;
    int resp;
}ChangeLogTaskContext;
uint64 forcCheckpointCnt = 0;
static ChangeLogTaskContext gl_ChangeLogTaskContext = { 0 };
static uint64 start = 0;
static uint64 us = 0;
static TaskDoneCallBack gl_parentCallback = NULL;

static int parentCallBack() {
    if (gl_parentCallback) {
        return gl_parentCallback();
    }
    return 0;
}
static int callback() {
    if (sendMsgToTask(TASK_ID_CHANGE_LOG, NULL)) {
        return 0;
    }
    return -1;
}

static int init() {
    initDiskLog();
    return initChangeLog();
}
static void thChangeLogFlush(void* in, void* out) {
    int err = 0;
    ChangeLogTaskContext* con = in;
    logItemType type = LOG_TYPE_WRITE;
    if (!con->cmd.data) {
        type = LOG_TYPE_REMOVE;
    }
    err = ChangeLogFlush(con->cmd.fileId, type, con->cmd.seek, con->cmd.size, con->cmd.data? con->cmd.data->buff + con->cmd.offset:NULL);
    if (err) {
        //error
        ASSERT(TRUE, "Flush log ERROR\n");
        con->Bitmap.flushState = FLUSH_ERROR;
    }
    else {
        con->Bitmap.flushState = FLUSH_SUCCESS;
        //printm("FLUSH_SUCCESS\n");
    }
}
static int pushCommand(uint64 fileId, logItemType type, uint64 seek, BufferListInfo* data, uint64 offset, uint64 size, int* resp, void* callback) {
    return ChangeLogInsert(fileId, type, seek, data, offset, size, resp, callback);
}

int initChangeLogTask() {
    return init();
}
//处理客户端的请求
TaskState ChangeLogTask(void* inP) {
    int err = 0;
    ChangeLogTaskState* state = &(gl_ChangeLogTaskContext.state);
    LogCommandItem* cmd = &gl_ChangeLogTaskContext.cmd;
    switch (*state) {
    case CHANGE_LOG_TASK_IDLE: {
        err = ChangeLogPopCmd(&gl_ChangeLogTaskContext.cmd);
        if (err == 0) {
            //no any cmd
            //printm("*** all command handle complete ***\n");
            gl_parentCallback = NULL;
            return TASK_STATE_COMPLETE;
        }
    }
    case CHANGE_LOG_TASK_PREPARE: {
        *state = CHANGE_LOG_TASK_PREPARE;
        start = myCounterStart();
        err = ChangeLogFlushPrepare(cmd->fileId, cmd->size);
        if (err < 0) {
            *cmd->resp = -1;
            *state = CHANGE_LOG_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }
        else if (err == 1) {
            //need checkpoint async
            CheckPointTaskParam param = {
            .callback = NULL,
            .resp = NULL
            };
            err = sendMsgToTask(TASK_ID_CHECK_POINT, &param);//no need wait and check resp, just trigger
        }
        else if (err == 2) {
            //need checkpoint now! goto sync checkpoint
            if (CheckPointTaskIsIdle()) {
                CheckPointTaskParam param = {
                .callback = NULL,
                .resp = NULL
                };
                if (!sendMsgToTask(TASK_ID_CHECK_POINT, &param)) {
                    return TASK_STATE_RESTART;
                }
            }
            
            *state = CHANGE_LOG_TASK_CHECKPOINT_SYNC;
            return TASK_STATE_RESTART;
        }
        else if(err == 0) {
            //logF not full, keep insert
        }
        else {
            ASSERT(TRUE, "unkown response from ChangeLogFlushPrepare?\n");
        }
       // goto CHANGE_LOG_TASK_FLUSH_LOG;
    }
    case CHANGE_LOG_TASK_FLUSH_LOG: {
        *state = CHANGE_LOG_TASK_FLUSH_LOG;
        ThreadParam param = {
               .func = thChangeLogFlush,
               .input = &gl_ChangeLogTaskContext
        };

        gl_ChangeLogTaskContext.thFlush = ThreadRun(param);
        if (gl_ChangeLogTaskContext.thFlush) {
            *state = CHANGE_LOG_TASK_WAIT_FLUSH_LOG_DONE;
        }

        return TASK_STATE_RESTART;
    }
    case CHANGE_LOG_TASK_WAIT_FLUSH_LOG_DONE: {
        //check response
        if (gl_ChangeLogTaskContext.Bitmap.flushState == FLUSH_SUCCESS) {
            gl_ChangeLogTaskContext.Bitmap.flushState = FLUSH_IDLE;
            gl_ChangeLogTaskContext.thFlush = NULL;
            *gl_ChangeLogTaskContext.cmd.resp = 0;
            //static uint64 cnt = 0;
            //printm("[%llu]\n", ++cnt);
            *state = CHANGE_LOG_TASK_COMPLETE;
        }
        else if (gl_ChangeLogTaskContext.Bitmap.flushState == FLUSH_ERROR) {
            //may be buffer not enough
            gl_ChangeLogTaskContext.thFlush = NULL;
            *gl_ChangeLogTaskContext.cmd.resp = -1;
            gl_ChangeLogTaskContext.Bitmap.flushState = FLUSH_IDLE;
            *state = CHANGE_LOG_TASK_FLUSH_LOG;
            //try again
        }
        else {
            //running

        }
        
        return TASK_STATE_RESTART;
    }
    case CHANGE_LOG_TASK_CHECKPOINT_SYNC: {
        //keep checkpoint until complete
        if (CheckPointTaskIsIdle()) {
            forcCheckpointCnt++;
            *state = CHANGE_LOG_TASK_PREPARE;
        }
        return TASK_STATE_RESTART;
    }
    case CHANGE_LOG_TASK_COMPLETE: {
        gl_parentCallback = gl_ChangeLogTaskContext.cmd.callback;
        if (gl_parentCallback() == 0) {
            ChangeLogCleanCommandItem(&gl_ChangeLogTaskContext.cmd);
            us = myCounterUs(start);
            *state = CHANGE_LOG_TASK_IDLE;
        }
        
        //printLog("%llu\n", us);
        return TASK_STATE_RESTART;
    }
    case CHANGE_LOG_TASK_ERROR: {
        *gl_ChangeLogTaskContext.cmd.resp = CHANGE_LOG_TASK_ERROR;
        ChangeLogCleanCommandItem(&gl_ChangeLogTaskContext.cmd);
        *state = CHANGE_LOG_TASK_IDLE;
        return TASK_STATE_RESTART;
    }
    default:
        ASSERT(TRUE, "");
        break;
    }
    ASSERT(TRUE, "");
    //never hear
    return TASK_STATE_IDLE;
}

bool ChangeLogTaskMessage(void* parent) {
    int err = 0;
    ChangeLogTaskCmd* cmd = parent;
    if (((!cmd->data || cmd->size == 0) && cmd->type == LOG_TYPE_WRITE) || 
        (cmd->type == LOG_TYPE_REMOVE && (cmd->data)) ||
        !TaskCanAddSuccess(TASK_ID_CHANGE_LOG) || cmd->resp == NULL || cmd->callback == NULL) {
        ASSERT(TRUE, "change log command invalid input\n");
        return FALSE;
    }
    err = pushCommand(cmd->fileId, cmd->type, cmd->seek, cmd->data, cmd->offset, cmd->size, cmd->resp, cmd->callback);
    if (err) { 
        return FALSE;
    }
    err = addTaskToList(TASK_ID_CHANGE_LOG, &gl_ChangeLogTaskContext);
    
    return err;
}
int ChangeLogTaskCallBack() {
    return parentCallBack();
}