#include "MainLoopTask.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_MAIN_LOOP
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_MAIN_LOOP

typedef enum MainLoopTaskState_e {
    MAIN_LOOP_TASK_IDLE,
    MAIN_LOOP_TASK_RUNNING,
    MAIN_LOOP_TASK_COMPLETE,
}MainLoopTaskState;

typedef struct MainLoopTaskBitmap_t {
    uint32 bit1 : 1;
    uint32 bit2 : 1;
}MainLoopTaskBitmap;

typedef struct MainLoopTaskContext_t {
    MainLoopTaskState state;
    MainLoopTaskBitmap Bitmap;
    
}MainLoopTaskContext;

static MainLoopTaskContext gl_MainLoopTaskContext = { 0 };
static TaskDoneCallBack gl_parentCallBack = NULL;

static int callback() {
    if (sendMsgToTask(TASK_ID_MAIN_LOOP, &gl_MainLoopTaskContext)) {
        return 0;
    }
    return -1;
}
static int parentCallBack() {
    if (gl_parentCallBack) {
        return gl_parentCallBack();
    }
    return 0;
}

//处理客户端的请求
TaskState MainLoopTask(void* inP) {
    int err = 0;
    MainLoopTaskState* state = &(gl_MainLoopTaskContext.state);
    switch (*state) {
    case MAIN_LOOP_TASK_IDLE: {
        printm("Main loop task entry\n");
    }
    case MAIN_LOOP_TASK_RUNNING: {
        if (sendMsgToTask(TASK_ID_TABLE_HANDLE_TEST, NULL)) {
            *state = MAIN_LOOP_TASK_COMPLETE;
        }
      
        return TASK_STATE_RESTART;
    }
    case MAIN_LOOP_TASK_COMPLETE: {
       
        *state = MAIN_LOOP_TASK_IDLE;
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
bool MainLoopTaskMessage(void* parent) {
    int err = 0;
    void* param = parent;
    err = addTaskToList(TASK_ID_MAIN_LOOP, &gl_MainLoopTaskContext);

    return err;
}
int MainLoopTaskCallBack() {
    return parentCallBack();
}