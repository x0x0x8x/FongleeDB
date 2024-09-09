#include "TaskHandler.h"
#include "TableDeleteTask.h"
#include "TableSelectRowTask.h"
#include "MyTime.h"
#include "DiskLog.h"
#include "BufferManager.h"
#include "ThreadManager.h"
#include "ChangeLogTask.h"
#include "ReadTask.h"
#include "storageEngine.h"
#include "sqlCommandHandleHost.h"
#include "CheckPointTask.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_TABLE_DELETE
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TABLE_DELETE

typedef struct TableDeleteTaskBitmap_t {
    uint32 done : 1;
}TableDeleteTaskBitmap;

typedef enum TableDeleteTaskState_e {
    TABLE_DELETE_TASK_IDLE,
    TABLE_DELETE_TASK_PREPARE,
    TABLE_DELETE_TASK_SELECT_ROWS_SCAN,
    TABLE_DELETE_TASK_SELECT_ROW,
    TABLE_DELETE_TASK_SELECT_ROW_DONE,
    TABLE_DELETE_TASK_DELETE_ROW,
    TABLE_DELETE_TASK_DELETE_ROW_DONE,
    TABLE_DELETE_TASK_COMPLETE,
    TABLE_DELETE_TASK_ERROR,
}TableDeleteTaskState;

typedef struct TableDeleteTaskContext_t {
    TableDeleteTaskState state;
    TableDeleteTaskBitmap Bitmap;
    TableDeleteTaskCmd cmd;
    BufferListInfo* tmpRow;
    uint32 tmpRowOffset;

    uint64 curSelectingRowId;
    uint64 validRowId;
    uint64 matchCnt;
    int resp;
}TableDeleteTaskContext;

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
    if (addTaskToList(TASK_ID_TABLE_DELETE, gl_tmp)) {
        return 0;
    }
    return -1;
}

static int init() {
    int err = 0;
    err = initTableMgr(); if (err) { return err; }

    return 0;
}

int initTableDeleteTask() {
    return init();
}
//处理客户端的请求
TaskState TableDeleteTask(void* inP) {
    int err = 0;
    BufferListInfo* tmp = inP;
    gl_tmp = tmp;
    TableDeleteTaskContext* con = tmp->buff;
    TableDeleteTaskState* state = &(con->state);

    switch (*state) {
    case TABLE_DELETE_TASK_IDLE: {
        con->Bitmap.done = 0;
        
    }
    case TABLE_DELETE_TASK_PREPARE: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);


        *state = TABLE_DELETE_TASK_SELECT_ROWS_SCAN;
        return TASK_STATE_RESTART;
    }
    case TABLE_DELETE_TASK_SELECT_ROWS_SCAN: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);

        if (con->tmpRow) {
            //if match, delete
            bool match = FALSE;
            RowTag* tag = con->tmpRow->buff + con->tmpRowOffset;
            char* row = tag + 1;
            //SqlHandleViewExpStack(con->cmd.tk.whereStack); printm("\n");
            
            match = SqlHandleCalculateExpression(con->cmd.tableId, row, &con->cmd.tk.whereStack, TableMgrGetColumnValueByNameCB);
            
            if (!match) {
                con->curSelectingRowId++;
                freeBuffer(&con->tmpRow);
                con->tmpRowOffset = 0;
            }
            else {
                //goto delete
                con->matchCnt++;
                freeBuffer(&con->tmpRow);
                con->tmpRowOffset = 0;
                *state = TABLE_DELETE_TASK_DELETE_ROW;
                return TASK_STATE_RESTART;
            }
        }
        
        if (con->curSelectingRowId > table->maxValidRowId) {
            *state = TABLE_DELETE_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }

        if (!TableMgrCheckRVT(con->cmd.tableId, con->curSelectingRowId)) {
            con->curSelectingRowId++;
            return TASK_STATE_RESTART;
        }
        
        *state = TABLE_DELETE_TASK_SELECT_ROW;
        return TASK_STATE_RESTART;
    }
    case TABLE_DELETE_TASK_SELECT_ROW: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        con->validRowId = con->curSelectingRowId;
        TableSelectRowTaskCmd param = {
            .callBack = callBack,
            .resp = &con->resp,
            .row = &con->tmpRow,
            .rowId = con->curSelectingRowId,
            .tableId = con->cmd.tableId,
            .offset = &con->tmpRowOffset
        };
        if (sendMsgToTask(TASK_ID_TABLE_SELECT_ROW, &param)) {
            *state = TABLE_DELETE_TASK_SELECT_ROW_DONE;
            return TASK_STATE_SUSPEND;
        }

        return TASK_STATE_RESTART;
    }
    case TABLE_DELETE_TASK_SELECT_ROW_DONE: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        err = con->resp;
        if (err) {
            *con->cmd.resp = err;
            *state = TABLE_DELETE_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }

        *state = TABLE_DELETE_TASK_SELECT_ROWS_SCAN;
        return TASK_STATE_RESTART;
    }
    case TABLE_DELETE_TASK_DELETE_ROW: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        if (TableMgrCheckRowBloomFilter(con->cmd.tableId, con->curSelectingRowId)) {
            //dest row being read, wait
            return TASK_STATE_RESTART;
        }
        BufferListInfo* dummy = getBuffer(sizeof(RowTag), BUFFER_POOL);
        if (!dummy) {
            return TASK_STATE_RESTART;
        }
        err = TableMgrSetRowLock(con->cmd.tableId, con->curSelectingRowId); ASSERT(err, "set row lock fail?\n")
        ChangeLogTaskCmd param = {
            .callback = callBack,
            .resp = &con->resp,
            .data = dummy,
            .offset = 0,
            .fileId = GET_TABLE_FILE_ID(con->cmd.tableId),
            .seek = con->curSelectingRowId * (sizeof(RowTag) + table->rowSize),
            .size = sizeof(RowTag)
        };
        if (sendMsgToTask(TASK_ID_CHANGE_LOG, &param)) {
            *state = TABLE_DELETE_TASK_DELETE_ROW_DONE;
            return TASK_STATE_SUSPEND;
        }

        return TASK_STATE_RESTART;
    }
    case TABLE_DELETE_TASK_DELETE_ROW_DONE: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        TableMgrCleanRowLock(con->cmd.tableId, con->curSelectingRowId);
        err = con->resp;
        if(err){
            *con->cmd.resp = err;
            *state = TABLE_DELETE_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }
        TableMgrCleanRVT(con->cmd.tableId, con->curSelectingRowId);
        
        //max valid row id?
        freeBuffer(&con->tmpRow);
        con->curSelectingRowId++;
        table->rowNum--;
        *state = TABLE_DELETE_TASK_SELECT_ROWS_SCAN;
        return TASK_STATE_RESTART;
    }
    case TABLE_DELETE_TASK_COMPLETE: {
        *state = TABLE_DELETE_TASK_IDLE;
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        freeBuffer(&con->tmpRow);
        gl_parentCallback = con->cmd.callBack;
        if ((con->resp == 0) && (con->matchCnt > 0)) {
            *con->cmd.resp = 0;
        }
        else { *con->cmd.resp = -1; }
        
        table->maxValidRowId = TableMgrGetMaxValidRowIdByRVT(con->cmd.tableId);
        ASSERT(U64_MAX == table->maxValidRowId && table->rowNum > 0, "invalid max row id\n");
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

bool TableDeleteTaskMessage(void* parent) {
    int err = 0;
    TableDeleteTaskCmd* cmd = parent;
    TableHandle* table = GET_TABLE_HANDLE(cmd->tableId);
    if (!cmd->callBack || !cmd->resp || !cmd->tk.whereStack || !TaskCanAddSuccess(TASK_ID_TABLE_DELETE)) {
        ASSERT(TRUE, "TableDelete invalid input\n");
        return FALSE;
    }
    if (table->flag.isDroping) {
        return FALSE;
    }
    
    BufferListInfo* tmp = getBuffer(sizeof(TableDeleteTaskContext), BUFFER_POOL); if (!tmp) { return FALSE; }
    TableDeleteTaskContext* con = tmp->buff;
    memcpy(&con->cmd, cmd, sizeof(*cmd));    
    con->state = TABLE_DELETE_TASK_IDLE;

    err = addTaskToList(TASK_ID_TABLE_DELETE, tmp);
    if (!err) {
        freeBuffer(&tmp);
        //ASSERT(TRUE, "add task fail\n");
    }
    table->userNum++;
    return err;
}

int TableDeleteCallBack(){
    return parentCallBack();
}