#include "TaskHandler.h"
#include "TableCreateTask.h"
#include "MyTime.h"
#include "DiskLog.h"
#include "BufferManager.h"
#include "ThreadManager.h"
#include "ChangeLogTask.h"
#include "ReadTask.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_TABLE_CREATE
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TABLE_CREATE

typedef struct TableCreateTaskBitmap_t {
    uint32 done : 1;
}TableCreateTaskBitmap;

typedef enum TableCreateTaskState_e {
    TABLE_CREATE_TASK_IDLE,
    TABLE_CREATE_TASK_PREPARE,
    TABLE_CREATE_TASK_INSERT_LOG,
    TABLE_CREATE_TASK_RUN,
    TABLE_CREATE_TASK_WAIT_DONE,
    TABLE_CREATE_TASK_INIT_TABLE_INFO,
    TABLE_CREATE_TASK_INIT_COLUMNS_MAX_LEN,
    TABLE_CREATE_TASK_INIT_COLUMNS_MAX_LEN_DONE,
    TABLE_CREATE_TASK_COMPLETE,
}TableCreateTaskState;

typedef struct TableCreateTaskContext_t {
    TableCreateTaskState state;
    TableCreateTaskBitmap Bitmap;
    TableCreateTaskCmd cmd;
    uint32 offset;//temp
    int resp;
}TableCreateTaskContext;

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
    if (addTaskToList(TASK_ID_TABLE_CREATE, gl_tmp)) {
        return 0;
    }
    return -1;
}

static int init() {
    int err = 0;
    err = initTableMgr(); if (err) { return err; }

    return 0;
}

int initTableCreateTask() {
    return init();
}
//处理客户端的请求
TaskState TableCreateTask(void* inP) {
    int err = 0;
    BufferListInfo* tmp = inP;
    gl_tmp = tmp;
    TableCreateTaskContext* con = tmp->buff;
    TableCreateTaskState* state = &(con->state);

    switch (*state) {
    case TABLE_CREATE_TASK_IDLE: {
        con->Bitmap.done = 0;
        
    }
    case TABLE_CREATE_TASK_PREPARE: {
        con->cmd.tableId = TableMgrGetFreeTableId();
        if (con->cmd.tableId == INVALID_TABLE_ID) {
            *con->cmd.resp = -1;
            *state = TABLE_CREATE_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        table->flag.isValid = 1;

        if (table->infoCache) {
            *con->cmd.resp = -1;
            *state = TABLE_CREATE_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }

        table->infoCache = getBuffer(sizeof(TableInfo) + (con->cmd.columnCnt) * sizeof(SqlColumnItem), BUFFER_POOL);
        if (!table->infoCache) {
            ASSERT(TRUE, "create table info but buffer not enough");
            *con->cmd.resp = -2;
            *state = TABLE_CREATE_TASK_COMPLETE;
            return TASK_STATE_RESTART;
        }
        TableInfo* tInfo = table->infoCache->buff;
        tInfo->id = con->cmd.tableId;
        memcpy(tInfo->name, con->cmd.name->buff, strlen(con->cmd.name->buff));
        tInfo->colNum = con->cmd.columnCnt;
        for (uint32 i = 0; i < con->cmd.columnCnt; i++) {
            memcpy(&tInfo->colInfo[i], con->cmd.colInfoList->buff + i * (sizeof(SqlColumnItem)), sizeof(SqlColumnItem));
        }
    }
    case TABLE_CREATE_TASK_INSERT_LOG: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        BufferListInfo* data = getBuffer(table->infoCache->size, BUFFER_POOL); 
        if (!data) {
            return TASK_STATE_RESTART;
        }
        memcpy(data->buff, table->infoCache->buff, data->size);
        ChangeLogTaskCmd param = { 
            .fileId = GET_TABLE_INFO_FILE_ID(con->cmd.tableId),
            .data = data,
            .offset = 0,
            .seek = 0,
            .size = table->infoCache->size,
            .resp = &con->resp,
            .callback = callBack
        };
       
        err = sendMsgToTask(TASK_ID_CHANGE_LOG, &param);
        if(err){
            *state = TABLE_CREATE_TASK_WAIT_DONE;
            return TASK_STATE_SUSPEND;
        }
        else {
            BuffRelease(&data);
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_CREATE_TASK_WAIT_DONE: {
        //check response
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        *con->cmd.resp = con->resp;
        if (con->resp == 0) {
            *state = TABLE_CREATE_TASK_INIT_TABLE_INFO;
        }
        else {
            freeBuffer(&table->infoCache);
            *state = TABLE_CREATE_TASK_COMPLETE;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_CREATE_TASK_INIT_TABLE_INFO: {
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        table->info = table->infoCache->buff;
        for (uint32 i = 0; i < table->info->colNum; i++) {
            table->rowSize += sqlCmdGetColSize(table->info->colInfo + i);
        }

        err = TableMgrInitRVT(con->cmd.tableId);//may be fail, but table create cmd respons should not be change
        if (err) { printm("init rvt after create fail\n"); }
        table->columnMaxStrLen = getBuffer(table->info->colNum * sizeof(uint32), BUFFER_POOL); 
        if (!table->columnMaxStrLen) { printm("init columns max strlen fail\n"); }
        err = TableMgrInitRowBloomFilterByTable(con->cmd.tableId); if (err) {
            printm("init row bloomFilter fail\n");
            ASSERT(TRUE, "init row bloomFilter fail\n"); 
        }

        *state = TABLE_CREATE_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_CREATE_TASK_COMPLETE: {
        *state = TABLE_CREATE_TASK_IDLE;
        TableHandle* table = GET_TABLE_HANDLE(con->cmd.tableId);
        if (con->cmd.tableId != INVALID_TABLE_ID) {
            if (*con->cmd.resp) {
                //fail
                table->flag.isValid = 0;
                freeBuffer(table->infoCache);
            }
        }
        
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

bool TableCreateTaskMessage(void* parent) {
    int err = 0;
    TableCreateTaskCmd* cmd = parent;
    TableHandle* table = GET_TABLE_HANDLE(cmd->tableId);
    if (!cmd->name || !cmd->colInfoList || !TaskCanAddSuccess(TASK_ID_TABLE_CREATE)) {
        ASSERT(TRUE, "TableCreate invalid input\n");
        return FALSE;
    }
    if (table->flag.isDroping) {
        return FALSE;
    }
    BufferListInfo* tmp = getBuffer(sizeof(TableCreateTaskContext), BUFFER_POOL); if (!tmp) { return FALSE; }
    TableCreateTaskContext* con = tmp->buff;
    memcpy(&con->cmd, cmd, sizeof(*cmd));    
    con->state = TABLE_CREATE_TASK_IDLE;

    err = addTaskToList(TASK_ID_TABLE_CREATE, tmp);
    if (!err) {
        freeBuffer(&tmp);
        //ASSERT(TRUE, "add task fail\n");
    }
    table->userNum++;
    return err;
}

int TableCreateCallBack(){
    return parentCallBack();
}