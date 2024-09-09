#include "TaskHandler.h"
#include "TableUpdateTask.h"
#include "TableSelectRowTask.h"
#include "MyTime.h"
#include "DiskLog.h"
#include "BufferManager.h"
#include "ThreadManager.h"
#include "ChangeLogTask.h"
#include "ReadTask.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_TABLE_UPDATE
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TABLE_UPDATE

typedef struct TableUpdateTaskBitmap_t {
    uint32 done : 1;
}TableUpdateTaskBitmap;

typedef enum TableUpdateTaskState_e {
    TABLE_UPDATE_TASK_IDLE,
    TABLE_UPDATE_TASK_PREPARE,
    TABLE_UPDATE_TASK_LOAD_ROWS_SCAN,
    TABLE_UPDATE_TASK_LOAD_ROW,
    TABLE_UPDATE_TASK_LOAD_ROW_DONE,
    TABLE_UPDATE_TASK_UPDATE_COLUMNS,
    TABLE_UPDATE_TASK_UPDATE_COLUMNS_DONE,
    TABLE_UPDATE_TASK_COMPLETE,
    TABLE_UPDATE_TASK_ERROR,
}TableUpdateTaskState;

typedef struct TableUpdateTaskContext_t {
    TableUpdateTaskState state;
    TableUpdateTaskState lastState;
    TableUpdateTaskBitmap Bitmap;
    TableUpdateTaskCmd cmd;

    BufferListInfo* tmpRow;
    uint32 tmpRowOffset;

    BufferListInfo* index;
    
    uint64 curSelectingRowId;
    uint64 matchCnt;
    
    int resp;
}TableUpdateTaskContext;

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
    if (addTaskToList(TASK_ID_TABLE_UPDATE, gl_tmp)) {
        return 0;
    }
    return -1;
}

static int init() {
    int err = 0;
    err = initTableMgr(); if (err) { return err; }

    return 0;
}

int initTableUpdateTask() {
    return init();
}
//处理客户端的请求
TaskState TableUpdateTask(void* inP) {
    int err = 0;
    BufferListInfo* tmp = inP;
    gl_tmp = tmp;
    TableUpdateTaskContext* con = tmp->buff;
    TableUpdateTaskState* state = &(con->state);

    switch (*state) {
    case TABLE_UPDATE_TASK_IDLE: {
        con->Bitmap.done = 0;
        
    }
    case TABLE_UPDATE_TASK_PREPARE: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        //check if has where exp and index

        //scan all rows
        con->curSelectingRowId = 0;
        con->matchCnt = 0;
        
        *state = TABLE_UPDATE_TASK_LOAD_ROWS_SCAN;
        return TASK_STATE_RESTART;
    }
    case TABLE_UPDATE_TASK_LOAD_ROWS_SCAN: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        con->lastState = TABLE_UPDATE_TASK_LOAD_ROWS_SCAN;
        
        if (con->tmpRow) {
            ASSERT(con->tmpRow->size - con->tmpRowOffset < (sizeof(RowTag) + table->rowSize), "invalid row data\n")
            RowTag* tag = con->tmpRow->buff + con->tmpRowOffset;
            char* row = tag + 1;
            bool isMatch = TRUE;
            if (con->cmd.tk.whereStack) {
                isMatch = SqlHandleCalculateExpression(con->cmd.tableId, row, &con->cmd.tk.whereStack, TableMgrGetColumnValueByNameCB);
            }
            if (isMatch) {
                //update col value and write
#define GET_SET_EXP(buff) (*((BufferListInfo**)(buff)))
                BufferListInfo** setExp = con->cmd.tk.setList->buff;
                uint32 setCnt = con->cmd.tk.setList->size / sizeof(BufferListInfo*);

                for (uint32 i = 0; i < setCnt; i++, setExp++) {
                    BufferListInfo* destCol = NULL;
                    BufferListInfo* value = NULL;
                    BufferListInfo* tmp = GET_SET_EXP(setExp);
                    err = SqlHandleCalculateSetExpression(con->cmd.tableId, row, &tmp, &destCol, &value, TableMgrGetColumnValueByNameCB); if (err) {
                        printm("calculate set exp fail when update\n");
                        ASSERT(TRUE, "");
                        break;
                    }
                    valueInfo* colNameInfo = destCol->buff;
                    char* colName = colNameInfo + 1;
                    uint64 colId = TableMgrGetColIdByName(con->cmd.tableId, colName, destCol->size - sizeof(valueInfo));
                    valueInfo* valueInfo1 = value->buff;
                    char* newValue = valueInfo1 + 1;
                    uint32 newValueSize = value->size - sizeof(valueInfo);
                    err = TableMgrUpdateColumnToRowBuffer(con->cmd.tableId, row, colId, newValue, newValueSize);
                    if (err) {
                        con->resp = -3;
                        TableMgrCleanRowBloomFilter(con->cmd.tableId, con->curSelectingRowId);
                        *state = TABLE_UPDATE_TASK_COMPLETE;
                        return TASK_STATE_RESTART;
                    }
                    uint32* maxLen = table->columnMaxStrLen->buff;
                    if (newValueSize > maxLen[colId]) {
                        maxLen[colId] = newValueSize;
                    }
                }
                
                con->matchCnt++;
                *state = TABLE_UPDATE_TASK_UPDATE_COLUMNS;
                return TASK_STATE_RESTART;
            }
            else {
                //load next row
                TableMgrCleanRowBloomFilter(con->cmd.tableId, con->curSelectingRowId);
                
                con->curSelectingRowId++;
            }
            freeBuffer(&con->tmpRow);
            con->tmpRowOffset = 0;
        }

        
        if (con->curSelectingRowId > table->maxValidRowId) {
            //for complete
            con->resp = 0;
            *state = TABLE_UPDATE_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }

        if (!TableMgrCheckRVT(con->cmd.tableId, con->curSelectingRowId)) {
            //continue
            con->curSelectingRowId++;
        }
        else {
            //load cur row
            if (TableMgrCheckRowLock(con->cmd.tableId, con->curSelectingRowId)) {
                //retry
                //is deleting
            }
            else {
                err = TableMgrSetRowBloomFilter(con->cmd.tableId, con->curSelectingRowId);
                if (err) {
                    //retry
                    //bloomFilter full
                }
                else {
                    *state = TABLE_UPDATE_TASK_LOAD_ROW;
                }
            }
        }
        

        return TASK_STATE_RESTART;
    }
    case TABLE_UPDATE_TASK_LOAD_ROW: {
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
            *state = TABLE_UPDATE_TASK_LOAD_ROW_DONE;
            return TASK_STATE_SUSPEND;
        }

        return TASK_STATE_RESTART;
    }
    case TABLE_UPDATE_TASK_LOAD_ROW_DONE: {
        err = con->resp;
        if (err) {
            *con->cmd.resp = err;
            *state = TABLE_UPDATE_TASK_COMPLETE;
        }
        *state = con->lastState;
        return TASK_STATE_RESTART;
    }
    case TABLE_UPDATE_TASK_UPDATE_COLUMNS: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        ChangeLogTaskCmd param = {
            .callback = callBack,
            .data = con->tmpRow,
            .fileId = GET_TABLE_FILE_ID(con->cmd.tableId),
            .offset = con->tmpRowOffset,
            .resp = &con->resp,
            .seek = con->curSelectingRowId * (sizeof(RowTag) + table->rowSize),
            .size = (sizeof(RowTag) + table->rowSize)
        };
        if (sendMsgToTask(TASK_ID_CHANGE_LOG, &param)) {
            con->tmpRow = NULL;
            *state = TABLE_UPDATE_TASK_UPDATE_COLUMNS_DONE;
            return TASK_STATE_SUSPEND;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_UPDATE_TASK_UPDATE_COLUMNS_DONE: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        TableMgrCleanRowBloomFilter(con->cmd.tableId, con->curSelectingRowId);
        err = con->resp;
        if (err) {
            *state = TABLE_UPDATE_TASK_COMPLETE;
        }
        else {
            freeBuffer(&con->tmpRow);
            con->curSelectingRowId++;
            *state = TABLE_UPDATE_TASK_LOAD_ROWS_SCAN;
        }

        return TASK_STATE_RESTART;
    }
    case TABLE_UPDATE_TASK_COMPLETE: {
        *state = TABLE_UPDATE_TASK_IDLE;        
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        freeBuffer(&con->tmpRow);
        freeBuffer(&con->index);

        gl_parentCallback = con->cmd.callBack;
        *con->cmd.resp = con->resp;
        
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

bool TableUpdateTaskMessage(void* parent) {
    int err = 0;
    TableUpdateTaskCmd* cmd = parent;
    TableHandle* table = GET_TABLE_HANDLE(cmd->tableId);
    if (!cmd->callBack || !cmd->resp || !cmd->tk.setList || !TaskCanAddSuccess(TASK_ID_TABLE_UPDATE)) {
        ASSERT(TRUE, "TableUpdate invalid input\n");
        return FALSE;
    }
    if (table->flag.isDroping) {
        return FALSE;
    }
    
    BufferListInfo* tmp = getBuffer(sizeof(TableUpdateTaskContext), BUFFER_POOL); if (!tmp) { return FALSE; }
    TableUpdateTaskContext* con = tmp->buff;
    memcpy(&con->cmd, cmd, sizeof(*cmd));    
    con->state = TABLE_UPDATE_TASK_IDLE;

    err = addTaskToList(TASK_ID_TABLE_UPDATE, tmp);
    if (!err) {
        freeBuffer(&tmp);
        //ASSERT(TRUE, "add task fail\n");
    }
    table->userNum++;
    return err;
}

int TableUpdateCallBack(){
    return parentCallBack();
}