#include "TaskHandler.h"
#include "TableInsertTask.h"
#include "MyTime.h"
#include "DiskLog.h"
#include "BufferManager.h"
#include "ThreadManager.h"
#include "ChangeLogTask.h"
#include "ReadTask.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_TABLE_INSERT
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TABLE_INSERT

typedef struct TableInsertTaskBitmap_t {
    uint32 done : 1;
}TableInsertTaskBitmap;

typedef enum TableInsertTaskState_e {
    TABLE_INSERT_TASK_IDLE,
    TABLE_INSERT_TASK_PREPARE,
    TABLE_INSERT_TASK_INSERT_LOG,
    TABLE_INSERT_TASK_WAIT_DONE,
    TABLE_INSERT_TASK_COMPLETE,
    TABLE_INSERT_TASK_ERROR,
}TableInsertTaskState;

typedef struct TableInsertTaskContext_t {
    TableInsertTaskState state;
    TableInsertTaskBitmap Bitmap;
    TableInsertTaskCmd cmd;
    BufferListInfo* rowData;
    uint64 rowId;
    int64 fileSeek;
    int resp;
}TableInsertTaskContext;

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
    if (addTaskToList(TASK_ID_TABLE_INSERT, gl_tmp)) {
        return 0;
    }
    return -1;
}

static int init() {
    int err = 0;
    err = initTableMgr(); if (err) { return err; }

    return 0;
}

int initTableInsertTask() {
    return init();
}
//处理客户端的请求
TaskState TableInsertTask(void* inP) {
    int err = 0;
    BufferListInfo* tmp = inP;
    gl_tmp = tmp;
    TableInsertTaskContext* con = tmp->buff;
    TableInsertTaskState* state = &(con->state);

    switch (*state) {
    case TABLE_INSERT_TASK_IDLE: {
        con->Bitmap.done = 0;
        
    }
    case TABLE_INSERT_TASK_PREPARE: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        if (con->cmd.columnInfoList->size / sizeof(SqlInsertColumnInfo) != table->info->colNum) {
            *con->cmd.resp = -1;
            *state = TABLE_INSERT_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }

        con->rowData = getBuffer(sizeof(RowTag) + table->rowSize, BUFFER_POOL); 
        if (!con->rowData) { 
            return TASK_STATE_RESTART;
        }
        ASSERT(table->freeRowId == table->maxValidRowId, "free row id == max valid row id\n");
        con->rowId = table->freeRowId;
        uint64 last = table->freeRowId;
        table->freeRowId = TableMgrFindNextFreeRow(con->cmd.tableId, table->freeRowId + 1);
        RowTag* tag = con->rowData->buff;
        char* row = tag + 1;

        SqlInsertColumnInfo* info = con->cmd.columnInfoList->buff;
        char* value = info + 1;
        for (uint32 i = 0; i < table->info->colNum; i++) {
            uint64 colId = TableMgrGetColIdByName(con->cmd.tableId, info->name, strlen(info->name));
            if (colId == INVALID_COLUMN_ID) {
                *con->cmd.resp = -2;
                *state = TABLE_INSERT_TASK_COMPLETE;
                return TASK_STATE_RESTART;
            }
            if (table->info->colInfo[colId].strMaxLen < info->dataLen) {
                *con->cmd.resp = -3;
                *state = TABLE_INSERT_TASK_COMPLETE;
                return TASK_STATE_RESTART;
            }

            char* colValuePtr = TableMgrGetColValuePtrById(con->cmd.tableId, row, colId);
            memcpy(colValuePtr, value, info->dataLen);

            uint32 curValueStrLen = sqlCmdGetColumnValueStrLen(value, table->info->colInfo[colId].strMaxLen, info->type);
            uint32* maxLenList = table->columnMaxStrLen->buff;
            maxLenList[i] = MAX(maxLenList[i], curValueStrLen);

            info = value + info->dataLen;
            value = info + 1;
        }

        con->fileSeek = con->rowId * (con->rowData->size);
        tag->isValid = 1;

        *state = TABLE_INSERT_TASK_INSERT_LOG;
        return TASK_STATE_RESTART;
    }
    case TABLE_INSERT_TASK_INSERT_LOG: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);

        ChangeLogTaskCmd param = { 
            .fileId = GET_TABLE_FILE_ID(con->cmd.tableId),
            .data = con->rowData,
            .offset = 0,
            .seek = con->fileSeek,
            .size = con->rowData->size,
            .resp = &con->resp,
            .callback = callBack
        };
       
        err = sendMsgToTask(TASK_ID_CHANGE_LOG, &param);
        if(err){
            *state = TABLE_INSERT_TASK_WAIT_DONE;
            return TASK_STATE_SUSPEND;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_INSERT_TASK_WAIT_DONE: {
        //check response
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        *con->cmd.resp = con->resp;
        ASSERT(con->rowId == table->maxValidRowId && table->maxValidRowId > 0, "max row id is inserted row id?\n");
        if (con->resp == 0) {
            if (con->rowId > table->maxValidRowId || table->maxValidRowId == U64_MAX) {
                table->maxValidRowId = con->rowId;
            }
            ASSERT(TableMgrCheckRVT(con->cmd.tableId, con->rowId), "inserted valid row?\n");
            err = TableMgrSetRVT(con->cmd.tableId, con->rowId); ASSERT(err, "set rvt after insert fail\n");
        }
        *state = TABLE_INSERT_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_INSERT_TASK_COMPLETE: {
        *state = TABLE_INSERT_TASK_IDLE;
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        
        gl_parentCallback = con->cmd.callBack;
        err = *con->cmd.resp;
        if (err) {
            if (table->freeRowId > con->rowId) {
                table->freeRowId = con->rowId;
            }
        }
        else {
            table->rowNum++;
        }
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

bool TableInsertTaskMessage(void* parent) {
    int err = 0;
    TableInsertTaskCmd* cmd = parent;
    TableHandle* table = GET_TABLE_HANDLE(cmd->tableId);
    if (!cmd->columnInfoList || !TaskCanAddSuccess(TASK_ID_TABLE_INSERT)) {
        ASSERT(TRUE, "TableInsert invalid input\n");
        return FALSE;
    }
    if (table->flag.isDroping) {
        return FALSE;
    }
    
    BufferListInfo* tmp = getBuffer(sizeof(TableInsertTaskContext), BUFFER_POOL); if (!tmp) { return FALSE; }
    TableInsertTaskContext* con = tmp->buff;
    memcpy(&con->cmd, cmd, sizeof(*cmd));    
    con->state = TABLE_INSERT_TASK_IDLE;

    err = addTaskToList(TASK_ID_TABLE_INSERT, tmp);
    if (!err) {
        freeBuffer(&tmp);
        //ASSERT(TRUE, "add task fail\n");
    }
    table->userNum++;
    return err;
}

int TableInsertCallBack(){
    return parentCallBack();
}