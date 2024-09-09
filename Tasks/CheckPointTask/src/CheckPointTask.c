#include "TaskHandler.h"
#include "CheckPointTask.h"
#include "ChangeLog.h"
#include "ThreadManager.h"
#include "MyTime.h"
#include "DiskLog.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_CHECK_POINT
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_CHECK_POINT

typedef struct CheckPointTaskBitmap_t {
    uint8 begin : 1;    //used for foreground
    uint8 done : 1; 

}CheckPointTaskBitmap;
typedef struct CheckPointTaskContext_t {
    CheckPointTaskState state;
    CheckPointTaskBitmap bitmap;
    CheckPointTaskParam cmd;
    ThreadPoolSlot* thUpdateToStorage;
    ThreadPoolSlot* thFlushToDisk;
    int resp;
}CheckPointTaskContext;

static CheckPointTaskContext gl_CheckPointTaskContext = { 0 };
static uint64 start = 0;
static uint64 us = 0;

static void thUpdateLogToStorage(void* in, void* out) {
    //push log to storage
    gl_CheckPointTaskContext.resp = ChangeLogUpdateBackgroundLogToStorage();
    gl_CheckPointTaskContext.bitmap.done = 1;
}
static void thFlushToDisk(void* in, void* out) {
    ChangeLogCheckpointBegin();
    gl_CheckPointTaskContext.bitmap.done = 1;
}
static int parentCallBack() {
    if (gl_CheckPointTaskContext.cmd.callback) {
        return gl_CheckPointTaskContext.cmd.callback();
    }
    return 0;
}
static int callback() {
    return 0;
}
static int init() {
    return 0;
}
static CheckPointTaskState getCheckPointTaskState() {
    return gl_CheckPointTaskContext.state;
}

int initCheckPointTask() {
    return init();
}
//处理客户端的请求
TaskState CheckPointTask(void* inP) {
    int err = 0;
    CheckPointTaskState* state = &(gl_CheckPointTaskContext.state);
    switch (*state) {
    case CHECK_POINT_TASK_IDLE: {
        ASSERT(gl_CheckPointTaskContext.thUpdateToStorage, "thUpdateToStorage thead not NULL\n");
        ASSERT(gl_CheckPointTaskContext.thFlushToDisk, "thFlushToDisk thead not NULL\n");
        ASSERT(gl_CheckPointTaskContext.resp, "int err not 0\n");
        //printm("*** Check point begin ***\n");
        gl_CheckPointTaskContext.bitmap.done = 0;
        start = myCounterStart();
    }
    case CHECK_POINT_TASK_UPDATE_TO_STORAGE: {
        *state = CHECK_POINT_TASK_UPDATE_TO_STORAGE;
        ThreadParam param = {
        .func = thUpdateLogToStorage,
        };
        gl_CheckPointTaskContext.thUpdateToStorage = ThreadRun(param);
        if (gl_CheckPointTaskContext.thUpdateToStorage) {
            *state = CHECK_POINT_TASK_WAIT_UPDATE_TO_STORAGE_DONE;
        }
        return TASK_STATE_RESTART;
    }
    case CHECK_POINT_TASK_WAIT_UPDATE_TO_STORAGE_DONE: {
        *state = CHECK_POINT_TASK_WAIT_UPDATE_TO_STORAGE_DONE;
        if (gl_CheckPointTaskContext.bitmap.done) {
            gl_CheckPointTaskContext.bitmap.done = 0;
            gl_CheckPointTaskContext.thUpdateToStorage = NULL;
            if (gl_CheckPointTaskContext.resp) {
                //error, retry
                *state = CHECK_POINT_TASK_UPDATE_TO_STORAGE;
                return TASK_STATE_RESTART;
            }
            *state = CHECK_POINT_TASK_FLUSH_TO_DISK;
        }
        return TASK_STATE_RESTART;
    }
    case CHECK_POINT_TASK_FLUSH_TO_DISK: {
        *state = CHECK_POINT_TASK_FLUSH_TO_DISK;
        ThreadParam param = {
        .func = thFlushToDisk,
        };
        gl_CheckPointTaskContext.thFlushToDisk = ThreadRun(param);
        if (gl_CheckPointTaskContext.thFlushToDisk) {
            *state = CHECK_POINT_TASK_WAIT_FLUSH_TO_DISK_DONE;
        }
        return TASK_STATE_RESTART;
    }
    case CHECK_POINT_TASK_WAIT_FLUSH_TO_DISK_DONE: {
        *state = CHECK_POINT_TASK_WAIT_FLUSH_TO_DISK_DONE;
        if (gl_CheckPointTaskContext.bitmap.done) {
            gl_CheckPointTaskContext.bitmap.done = 0;
            gl_CheckPointTaskContext.thFlushToDisk = NULL;
            *state = CHECK_POINT_TASK_CLEAN;
        }
        return TASK_STATE_RESTART;
    }
    case CHECK_POINT_TASK_CLEAN: {
        bool isReading = ChangeLogIsReadingBLog();
        if (!isReading) {
            ChangeLogCheckpointDone();
            *state = CHECK_POINT_TASK_COMPLETE;
        }
        return TASK_STATE_RESTART;
    }
    case CHECK_POINT_TASK_COMPLETE: {
        *state = CHECK_POINT_TASK_IDLE;
        gl_CheckPointTaskContext.bitmap.begin = 0;
        gl_CheckPointTaskContext.bitmap.done = 0;
        us = myCounterUs(start);
        //printLog("%llu\n", us);
        if (gl_CheckPointTaskContext.cmd.resp) {
            *gl_CheckPointTaskContext.cmd.resp = 0;
        }
        return TASK_STATE_COMPLETE;
    }
    case CHECK_POINT_TASK_ERROR: {
        ASSERT(TRUE, "checkpoint error!");
        exit(0);
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

bool CheckPointTaskMessage(void* parent) {
    int err = 0;
    CheckPointTaskParam* param = parent;
    if (gl_CheckPointTaskContext.state != CHECK_POINT_TASK_IDLE && gl_CheckPointTaskContext.bitmap.begin) {
        return TRUE;
    }
    memcpy(&gl_CheckPointTaskContext.cmd, param, sizeof(CheckPointTaskParam));
    err = addTaskToList(TASK_ID_CHECK_POINT, &gl_CheckPointTaskContext);
    if (err == TRUE) {
        gl_CheckPointTaskContext.bitmap.begin = 1;
    }
    return err;
}
int CheckPointCallBack() {
    return parentCallBack();
}
bool CheckPointTaskIsIdle() {
    return gl_CheckPointTaskContext.bitmap.begin == 0 && gl_CheckPointTaskContext.state == CHECK_POINT_TASK_IDLE;
}