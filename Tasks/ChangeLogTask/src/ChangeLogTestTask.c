#include "TaskHandler.h"
#include "ChangeLogTestTask.h"
#include "ChangeLogTask.h"
#include "ChangeLog.h"
#include "ReadTask.h"
#include "storageEngine.h"
#include "MyTime.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_CHANGE_LOG_TEST
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_CHANGE_LOG_TEST

typedef enum ChangeLogTestTaskState_e {
    CHANGE_LOG_TEST_TASK_IDLE,
    CHANGE_LOG_TEST_TASK_WRITE,
    CHANGE_LOG_TEST_TASK_WAIT_WRITE_DONE,
    CHANGE_LOG_TEST_TASK_WRITE_COMPLETE,
    CHANGE_LOG_TEST_TASK_READ,
    CHANGE_LOG_TEST_TASK_WAIT_READ_DONE,
    CHANGE_LOG_TEST_TASK_READ_COMPLETE,
}ChangeLogTestTaskState;

typedef union ChangeLogTestTaskBitmap_t {
    uint32 dummy;
    struct {
        uint32 bit1 : 1;
        uint32 bit2 : 1;
    };
}ChangeLogTestTaskBitmap;

typedef struct ChangeLogTestTaskContext_t {
    ChangeLogTestTaskState state;
    ChangeLogTestTaskBitmap TCPBitmap;
    ChangeLogTaskCmd cmd;
    uint8 resp;
}ChangeLogTestTaskContext;
extern uint64 forcCheckpointCnt;
static ChangeLogTestTaskContext gl_ChangeLogTestTaskContext = { 0 };
static int num = 0x1;
static uint64 cnt = 0;
static uint64 maxCnt = BYTE_1K*64;
static uint64 size = 1777;
static uint64 start = 0;
static uint64 us = 0;
static uint64 maxUs = 0;
static uint64 tmpTime = 0;
static uint64 fileId = 10;

static uint64 seek = 0;
static BufferListInfo* load = NULL;
static uint64 offset = 0;

static int callback() {
    if (sendMsgToTask(TASK_ID_CHANGE_LOG_TEST, NULL)) {
        return 0;
    }
    return -1;
}

//处理客户端的请求
TaskState ChangeLogTestTask(void* inP) {
    int err = 0;
    ChangeLogTestTaskState* state = &(gl_ChangeLogTestTaskContext.state);
   
    switch (*state) {
    case CHANGE_LOG_TEST_TASK_IDLE: {
        printm("Change Log Test task entry\n");
        hide_cursor();
        *state = CHANGE_LOG_TEST_TASK_WRITE;
    }
    case CHANGE_LOG_TEST_TASK_WRITE: {
        uint32 continueCnt = 1;
        for (uint32 i = 0; i < continueCnt;i++) {
            gl_ChangeLogTestTaskContext.cmd.data = getBuffer(size, BUFFER_POOL_NORMAL);
            if (!gl_ChangeLogTestTaskContext.cmd.data) {
                return TASK_STATE_RESTART;
            }
            memset(gl_ChangeLogTestTaskContext.cmd.data->buff, num, gl_ChangeLogTestTaskContext.cmd.data->size);
            gl_ChangeLogTestTaskContext.cmd.fileId = fileId;
            gl_ChangeLogTestTaskContext.cmd.offset = 0;
            gl_ChangeLogTestTaskContext.cmd.resp = &gl_ChangeLogTestTaskContext.resp;
            gl_ChangeLogTestTaskContext.cmd.size = gl_ChangeLogTestTaskContext.cmd.data->size;
            gl_ChangeLogTestTaskContext.cmd.callback = callback;

            if (sendMsgToTask(TASK_ID_CHANGE_LOG, &gl_ChangeLogTestTaskContext.cmd)) {
                //printm("[%llu]\n", cnt);
                start = myCounterStart();
                *state = CHANGE_LOG_TEST_TASK_WAIT_WRITE_DONE;
                return TASK_STATE_SUSPEND;
            }
            else {
                static uint32 failCnt = 0;
                printm("push change log fail [%lu]\n", ++failCnt);
                BuffRelease(&gl_ChangeLogTestTaskContext.cmd.data);
                break;
            }
        }
        
        return TASK_STATE_RESTART;
    }
    case CHANGE_LOG_TEST_TASK_WAIT_WRITE_DONE: {
        *state = CHANGE_LOG_TEST_TASK_WAIT_WRITE_DONE;
        uint64 us = myCounterUs(start);
        tmpTime += us;
        num++;
        cnt++;
        gl_ChangeLogTestTaskContext.cmd.seek += gl_ChangeLogTestTaskContext.cmd.size;
        print_progress_bar(cnt * 100 / maxCnt); 
        //if (us > maxUs) {
        //    maxUs = us;
        //    printm("\tmax %llu us", maxUs);
        //}
        if (cnt >= maxCnt) {
            *state = CHANGE_LOG_TEST_TASK_WRITE_COMPLETE;
        }
        else {
            *state = CHANGE_LOG_TEST_TASK_WRITE;
        }
        return TASK_STATE_RESTART;
    }
    case CHANGE_LOG_TEST_TASK_WRITE_COMPLETE: {
        print_progress_bar(cnt * 100 / maxCnt);
        printm("\n*** pass *** "); printUsTick(tmpTime); printm("\n");
        num = 0x1;
        cnt = 0;
        us = 0;
        tmpTime = 0;
        start = myCounterStart();
        printm("FCheckpoint [%llu]\n", forcCheckpointCnt);
        *state = CHANGE_LOG_TEST_TASK_READ;
        return TASK_STATE_RESTART;
    }
    case CHANGE_LOG_TEST_TASK_READ: {
        
        ReadTaskCmd param = {
            .callback = callback,
            .data = &load,
            .fileId = fileId,
            .offset = &offset,
            .resp = &gl_ChangeLogTestTaskContext.resp,
            .seek = seek, 
            .size = size
        };

        if (sendMsgToTask(TASK_ID_READ, &param)) {
            *state = CHANGE_LOG_TEST_TASK_WAIT_READ_DONE;
            start = myCounterStart();
            return TASK_STATE_SUSPEND;
        }
        return TASK_STATE_RESTART;
    }
    case CHANGE_LOG_TEST_TASK_WAIT_READ_DONE: {
        static char tmp[4096] = { 0 };
        memset(tmp, num + cnt, size);
        ASSERT(strncmp(tmp, load->buff + offset, size) != 0, "read data not right\n");
        BuffRelease(&load);

        seek += size;
        cnt++;
        uint64 us = myCounterUs(start);
        tmpTime += us;
        print_progress_bar(cnt * 100 / maxCnt);
        if (cnt >= maxCnt) {
            *state = CHANGE_LOG_TEST_TASK_READ_COMPLETE;
        }
        else {
            *state = CHANGE_LOG_TEST_TASK_READ;
        }
        return TASK_STATE_RESTART;
    }
    case CHANGE_LOG_TEST_TASK_READ_COMPLETE: {
        print_progress_bar(cnt * 100 / maxCnt);
        show_cursor();
        printm("\n*** read pass *** "); printUsTick(tmpTime); printm("\n");
        exit(0);
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

bool ChangeLogTestTaskMessage(void* parent) {
    int err = 0;
    err = addTaskToList(TASK_ID_CHANGE_LOG_TEST, &gl_ChangeLogTestTaskContext);

    return err;
}