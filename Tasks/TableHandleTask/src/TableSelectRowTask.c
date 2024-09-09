#include "TaskHandler.h"
#include "TableSelectRowTask.h"
#include "MyTime.h"
#include "DiskLog.h"
#include "BufferManager.h"
#include "ThreadManager.h"
#include "ChangeLogTask.h"
#include "ReadTask.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_TABLE_SELECT_ROW
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TABLE_SELECT_ROW

typedef struct TableSelectRowTaskBitmap_t {
    uint32 done : 1;
}TableSelectRowTaskBitmap;

typedef enum TableSelectRowTaskState_e {
    TABLE_SELECT_ROW_TASK_IDLE,
    TABLE_SELECT_ROW_TASK_PREPARE,
    TABLE_SELECT_ROW_TASK_READ_ROW,
    TABLE_SELECT_ROW_TASK_READ_ROW_DONE,
    TABLE_SELECT_ROW_TASK_COMPLETE,
    TABLE_SELECT_ROW_TASK_ERROR,
}TableSelectRowTaskState;

typedef struct TableSelectRowTaskContext_t {
    TableSelectRowTaskState state;
    TableSelectRowTaskBitmap Bitmap;
    TableSelectRowTaskCmd cmd;

    int resp;
}TableSelectRowTaskContext;

static uint64 start = 0;
static uint64 us = 0;
static BufferListInfo* gl_tmp = NULL;
static TaskDoneCallBack gl_parentCallback = NULL;

static int parentCallBack() {
    if(gl_parentCallback)
        gl_parentCallback();
    return 0;
}
static int callBack() {
    ASSERT(!gl_tmp, "context buffer is null\n");
    if (addTaskToList(TASK_ID_TABLE_SELECT_ROW, gl_tmp)) {
        return 0;
    }
    return -1;
}

static int init() {
    int err = 0;

    return 0;
}

int initTableSelectRowTask() {
    return init();
}
//处理客户端的请求
TaskState TableSelectRowTask(void* inP) {
    int err = 0;
    BufferListInfo* tmp = inP;
    gl_tmp = tmp;
    TableSelectRowTaskContext* con = tmp->buff;
    TableSelectRowTaskState* state = &(con->state);

    switch (*state) {
    case TABLE_SELECT_ROW_TASK_IDLE: {

        
    }
    case TABLE_SELECT_ROW_TASK_PREPARE: {
        if (!TableMgrCheckRVT(con->cmd.tableId, con->cmd.rowId)) {
            //invalid row (has been deleted)
            *con->cmd.resp = 1;
            *state = TABLE_SELECT_ROW_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }
        if (TableMgrCheckRowLock(con->cmd.tableId, con->cmd.rowId)) {
            //retry, because may be delete fail
            return TASK_STATE_RESTART;
        }
        err = TableMgrSetRowBloomFilter(con->cmd.tableId, con->cmd.rowId);
        if (err) {
            //retry, because may be bloom solt is full (command number more than u8max) or initBloomFilter fail (buffer not enough)
            return TASK_STATE_RESTART;
        }
        //begin read row
        *state = TABLE_SELECT_ROW_TASK_READ_ROW;
    }
    case TABLE_SELECT_ROW_TASK_READ_ROW: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        ReadTaskCmd param = {
            .callback = callBack,
            .fileId = GET_TABLE_FILE_ID(con->cmd.tableId),
            .seek = con->cmd.rowId * (table->rowSize + sizeof(RowTag)),
            .size = (table->rowSize + sizeof(RowTag)),
            .offset = con->cmd.offset,
            .data = con->cmd.row, 
            .resp = &con->resp
        };
        //printm("read row[%llu]: seek[%llu] size[%lu]\n", con->cmd.rowId, param.seek, param.size);
        if (sendMsgToTask(TASK_ID_READ, &param)) {
            *state = TABLE_SELECT_ROW_TASK_READ_ROW_DONE;
            return TASK_STATE_SUSPEND;
        }

        return TASK_STATE_RESTART;
    }
    case TABLE_SELECT_ROW_TASK_READ_ROW_DONE: {
        TableMgrCleanRowBloomFilter(con->cmd.tableId, con->cmd.rowId);
        //check response
        err = con->resp;
        *con->cmd.resp = err;

        *state = TABLE_SELECT_ROW_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_SELECT_ROW_TASK_COMPLETE: {
        *state = TABLE_SELECT_ROW_TASK_IDLE;
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        gl_parentCallback = con->cmd.callBack;
        
        freeBuffer(&tmp);
        table->userNum--;
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

bool TableSelectRowTaskMessage(void* parent) {
    int err = 0;
    TableSelectRowTaskCmd* cmd = parent;
    TableHandle* table = GET_TABLE_HANDLE(cmd->tableId);
    if (!cmd->callBack || !cmd->resp || !cmd->row || !cmd->offset || !TaskCanAddSuccess(TASK_ID_TABLE_SELECT_ROW)) {
        ASSERT(TRUE, "TableSelectRow invalid input\n");
        return FALSE;
    }
    if (*cmd->row || table->flag.isDroping) {
        ASSERT(TRUE, "TableSelectRow invalid input 2\n");
        return FALSE;
    }
    
    BufferListInfo* tmp = getBuffer(sizeof(TableSelectRowTaskContext), BUFFER_POOL); if (!tmp) { return FALSE; }
    TableSelectRowTaskContext* con = tmp->buff;
    memcpy(&con->cmd, cmd, sizeof(*cmd));    
    con->state = TABLE_SELECT_ROW_TASK_IDLE;

    err = addTaskToList(TASK_ID_TABLE_SELECT_ROW, tmp);
    if (!err) {
        freeBuffer(&tmp);
        //ASSERT(TRUE, "add task fail\n");
    }
    table->userNum++;
    return err;
}

int TableSelectRowCallBack(){
    return parentCallBack();
}