#include "TaskHandler.h"
#include "TableSelectTask.h"
#include "TableSelectRowTask.h"
#include "MyTime.h"
#include "DiskLog.h"
#include "BufferManager.h"
#include "ThreadManager.h"
#include "ChangeLogTask.h"
#include "ReadTask.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_TABLE_SELECT
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TABLE_SELECT

typedef struct TableSelectTaskBitmap_t {
    uint32 done : 1;
}TableSelectTaskBitmap;

typedef enum TableSelectTaskState_e {
    TABLE_SELECT_TASK_IDLE,
    TABLE_SELECT_TASK_PREPARE,
    TABLE_SELECT_TASK_LOAD_ROWS_SCAN,
    TABLE_SELECT_TASK_LOAD_ROW,
    TABLE_SELECT_TASK_LOAD_ROW_DONE,
    TABLE_SELECT_TASK_WAIT_LOAD_DONE,
    TABLE_SELECT_TASK_SCREEN,
    TABLE_SELECT_TASK_COMPLETE,
    TABLE_SELECT_TASK_ERROR,
}TableSelectTaskState;

typedef struct TableSelectTaskContext_t {
    TableSelectTaskState state;
    TableSelectTaskState lastState;
    TableSelectTaskBitmap Bitmap;
    TableSelectTaskCmd cmd;
    BufferListInfo* load;
    BufferListInfo* tmpRow;
    uint32 tmpRowOffset;
    uint32 offset;
    uint64 loadHeadRowId;
    uint64 loadRowCnt;

    BufferListInfo* index;
    
    uint64 curSelectingRowId;
    uint64 matchCnt;
    
    int resp;
}TableSelectTaskContext;

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
    if (addTaskToList(TASK_ID_TABLE_SELECT, gl_tmp)) {
        return 0;
    }
    return -1;
}

static int init() {
    int err = 0;
    err = initTableMgr(); if (err) { return err; }

    return 0;
}

int initTableSelectTask() {
    return init();
}
//处理客户端的请求
TaskState TableSelectTask(void* inP) {
    int err = 0;
    BufferListInfo* tmp = inP;
    gl_tmp = tmp;
    TableSelectTaskContext* con = tmp->buff;
    TableSelectTaskState* state = &(con->state);

    switch (*state) {
    case TABLE_SELECT_TASK_IDLE: {
        con->Bitmap.done = 0;
        
    }
    case TABLE_SELECT_TASK_PREPARE: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        //check if has where exp and index

        //scan all rows
        con->loadHeadRowId = 0;
        con->curSelectingRowId = 0;
        con->matchCnt = 0;
        
        *state = TABLE_SELECT_TASK_LOAD_ROWS_SCAN;
        return TASK_STATE_RESTART;
    }
    case TABLE_SELECT_TASK_LOAD_ROWS_SCAN: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        con->lastState = TABLE_SELECT_TASK_LOAD_ROWS_SCAN;
        
        if (con->tmpRow) {
            uint32 destColCnt = con->cmd.tk.destColList->buff[0] == '*' ? table->info->colNum : con->cmd.tk.destColList->size / sizeof(uint64);
            uint64 totalSize = con->cmd.tk.destColList->buff[0] == '*' ? table->rowSize : TableMgrGetColumnIdListTotalValueSize(con->cmd.tableId, con->cmd.tk.destColList->buff, destColCnt);

            RowTag* tag = con->tmpRow->buff + con->tmpRowOffset;
            char* row = tag + 1;
            bool isMatch = TRUE;
            if (con->cmd.tk.whereStack) {
                isMatch = SqlHandleCalculateExpression(con->cmd.tableId, row, &con->cmd.tk.whereStack, TableMgrGetColumnValueByNameCB);
            }
            if (isMatch) {
                if (con->load) {
                    err = BuffRealloc(&con->load, con->load->size + totalSize, BUFFER_POOL); 
                }
                else {
                    con->load = getBuffer(totalSize, BUFFER_POOL); 
                    if (!con->load) { 
                        err = -2;
                    }
                }
                if (err) {
                    //retry
                    return TASK_STATE_RESTART;
                }

                char* outPtr = con->load->buff + con->matchCnt * totalSize;
                if (con->cmd.tk.destColList->buff[0] == '*') {
                    memcpy(outPtr, row, totalSize);
                }
                else {
                    uint64* destColId = con->cmd.tk.destColList->buff;
                    for (uint32 i = 0; i < destColCnt; i++, destColId++) {
                        char* colValue = TableMgrGetColValuePtrById(con->cmd.tableId, row, *destColId);
                        memcpy(outPtr, colValue, sqlCmdGetColSize(table->info->colInfo + *destColId));
                        outPtr += (sqlCmdGetColSize(table->info->colInfo + *destColId));
                    }
                }
                con->matchCnt++;                
            }
            freeBuffer(&con->tmpRow);
            con->tmpRowOffset = 0;
        }

        if (con->curSelectingRowId > table->maxValidRowId) {
            //for complete
            *state = TABLE_SELECT_TASK_WAIT_LOAD_DONE;
            return TASK_STATE_RESTART;
        }

        *state = TABLE_SELECT_TASK_LOAD_ROW;
        return TASK_STATE_RESTART;
    }
    case TABLE_SELECT_TASK_LOAD_ROW: {
        //check response
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        TableSelectRowTaskCmd param = {
            .callBack = callBack,
            .resp = &con->resp,
            .row = &con->tmpRow,
            .offset = &con->tmpRowOffset,
            .rowId = con->curSelectingRowId,
            .tableId = con->cmd.tableId
        };
        
        if (sendMsgToTask(TASK_ID_TABLE_SELECT_ROW, &param)) {
            *state = TABLE_SELECT_TASK_LOAD_ROW_DONE;
            return TASK_STATE_SUSPEND;
        }

        return TASK_STATE_RESTART;
    }
    case TABLE_SELECT_TASK_LOAD_ROW_DONE: {
        err = con->resp;
        if (err == 1) {
            //not valid row
            freeBuffer(&con->tmpRow);
            con->resp = 0;
        }
        else if (err < 0) {
            *con->cmd.resp = err;
            *state = TABLE_SELECT_TASK_COMPLETE;
        }
        con->curSelectingRowId++;
        *state = con->lastState;
        return TASK_STATE_RESTART;
    }
    case TABLE_SELECT_TASK_WAIT_LOAD_DONE: {
        //check response
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        err = con->resp;
        if (err) {
            *con->cmd.resp = err;
            *state = TABLE_SELECT_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }

        *state = TABLE_SELECT_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_SELECT_TASK_COMPLETE: {
        *state = TABLE_SELECT_TASK_IDLE;
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        if (con->resp) {
            freeBuffer(&con->load);
        }
        *con->cmd.out = con->load;
        *con->cmd.resp = con->resp;
        if (!con->load) {
            *con->cmd.resp = 1;//not match
        }
        freeBuffer(&con->tmpRow);
        freeBuffer(&con->index);

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

bool TableSelectTaskMessage(void* parent) {
    int err = 0;
    TableSelectTaskCmd* cmd = parent;
    TableHandle* table = GET_TABLE_HANDLE(cmd->tableId);
    if (!cmd->callBack || !cmd->resp || !TaskCanAddSuccess(TASK_ID_TABLE_SELECT)) {
        ASSERT(TRUE, "TableSelect invalid input\n");
        return FALSE;
    }
    if (table->flag.isDroping) {
        return FALSE;
    }
    
    BufferListInfo* tmp = getBuffer(sizeof(TableSelectTaskContext), BUFFER_POOL); if (!tmp) { return FALSE; }
    TableSelectTaskContext* con = tmp->buff;
    memcpy(&con->cmd, cmd, sizeof(*cmd));    
    con->state = TABLE_SELECT_TASK_IDLE;

    err = addTaskToList(TASK_ID_TABLE_SELECT, tmp);
    if (!err) {
        freeBuffer(&tmp);
        //ASSERT(TRUE, "add task fail\n");
    }
    table->userNum++;
    return err;
}

int TableSelectCallBack(){
    return parentCallBack();
}