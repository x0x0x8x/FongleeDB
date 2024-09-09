#include "TaskHandler.h"
#include "TableDropTask.h"
#include "MyTime.h"
#include "DiskLog.h"
#include "BufferManager.h"
#include "ThreadManager.h"
#include "ChangeLogTask.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_TABLE_DROP
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TABLE_DROP

typedef struct TableDropTaskBitmap_t {
    uint32 done : 1;
}TableDropTaskBitmap;

typedef enum TableDropTaskState_e {
    TABLE_DROP_TASK_IDLE,
    TABLE_DROP_TASK_PREPARE,
    TABLE_DROP_TASK_REMOVE_TABLE,
    TABLE_DROP_TASK_REMOVE_TABLE_DONE,
    TABLE_DROP_TASK_REMOVE_TABLE_INFO,
    TABLE_DROP_TASK_REMOVE_TABLE_INFO_DONE,
    TABLE_DROP_TASK_COMPLETE,
}TableDropTaskState;

typedef struct TableDropTaskContext_t {
    TableDropTaskState state;
    TableDropTaskBitmap Bitmap;
    TableDropTaskCmd cmd;
    uint32 offset;//temp
    int resp;
}TableDropTaskContext;

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
    if (addTaskToList(TASK_ID_TABLE_DROP, gl_tmp)) {
        return 0;
    }
    return -1;
}

static int init() {
    int err = 0;
    err = initTableMgr(); if (err) { return err; }

    return 0;
}

int initTableDropTask() {
    return init();
}
//处理客户端的请求
TaskState TableDropTask(void* inP) {
    int err = 0;
    BufferListInfo* tmp = inP;
    gl_tmp = tmp;
    TableDropTaskContext* con = tmp->buff;
    TableDropTaskState* state = &(con->state);

    switch (*state) {
    case TABLE_DROP_TASK_IDLE: {
        con->Bitmap.done = 0;
        
    }
    case TABLE_DROP_TASK_PREPARE: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        
    }
    case TABLE_DROP_TASK_REMOVE_TABLE: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        if (table->rowNum > 0) {
            ChangeLogTaskCmd param = {
                .type = LOG_TYPE_REMOVE,
                .fileId = GET_TABLE_FILE_ID(con->cmd.tableId),
                .data = NULL,
                .offset = 0,
                .seek = 0,
                .size = 0,
                .resp = &con->resp,
                .callback = callBack
            };

            err = sendMsgToTask(TASK_ID_CHANGE_LOG, &param);
            if (err) {
                *state = TABLE_DROP_TASK_REMOVE_TABLE_DONE;
                return TASK_STATE_SUSPEND;
            }
        }
        else {
            //no table file
#ifdef DEBUG_TABLE_DROP
            if (StorageIsFileExist(GET_TABLE_FILE_ID(con->cmd.tableId))) {
                ASSERT(TRUE, "rowNum > 0 but table file exist\n");
            }
#endif // DEBUG_TABLE_DROP
            * state = TABLE_DROP_TASK_REMOVE_TABLE_INFO;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_DROP_TASK_REMOVE_TABLE_DONE: {
        //check response
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        if (con->resp){
            *con->cmd.resp = con->resp;
            *state = TABLE_DROP_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }
        *state = TABLE_DROP_TASK_REMOVE_TABLE_INFO;
        return TASK_STATE_RESTART;
    }
    case TABLE_DROP_TASK_REMOVE_TABLE_INFO: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        ChangeLogTaskCmd param = {
            .type = LOG_TYPE_REMOVE,
            .fileId = GET_TABLE_INFO_FILE_ID(con->cmd.tableId),
            .data = NULL,
            .offset = 0,
            .seek = 0,
            .size = 0,
            .resp = &con->resp,
            .callback = callBack
        };

        err = sendMsgToTask(TASK_ID_CHANGE_LOG, &param);
        if (err) {
            *state = TABLE_DROP_TASK_REMOVE_TABLE_INFO_DONE;
            return TASK_STATE_SUSPEND;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_DROP_TASK_REMOVE_TABLE_INFO_DONE: {
        //check response
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        *con->cmd.resp = con->resp;

        *state = TABLE_DROP_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_DROP_TASK_COMPLETE: {
        *state = TABLE_DROP_TASK_IDLE;
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        if (*con->cmd.resp == 0) {
            //success, clear table context
            TableMgrCleanTable(con->cmd.tableId);
        }
        gl_parentCallback = con->cmd.callBack;
        freeBuffer(&tmp);
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

bool TableDropTaskMessage(void* parent) {
    int err = 0;
    TableDropTaskCmd* cmd = parent;
    TableHandle* table = GET_TABLE_HANDLE(cmd->tableId);
    if (table->flag.isDroping || !cmd->callBack || !cmd->resp || cmd->tableId == INVALID_TABLE_ID) {
        ASSERT(TRUE, "TableDrop invalid input\n");
        return FALSE;
    }
    BufferListInfo* tmp = getBuffer(sizeof(TableDropTaskContext), BUFFER_POOL); if (!tmp) { return FALSE; }
    TableDropTaskContext* con = tmp->buff;
    memcpy(&con->cmd, cmd, sizeof(*cmd));    
    con->state = TABLE_DROP_TASK_IDLE;

    err = addTaskToList(TASK_ID_TABLE_DROP, tmp);
    if (!err) {
        freeBuffer(&tmp);
        //ASSERT(TRUE, "add task fail\n");
    }
    table->flag.isDroping = 1;
    return err;
}

int TableDropCallBack(){
    return parentCallBack();
}