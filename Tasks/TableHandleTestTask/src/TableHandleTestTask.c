#include "TaskHandler.h"
#include "TableHandleTestTask.h"
#include "MyTime.h"
#include "DiskLog.h"
#include "BufferManager.h"
#include "TableCreateTask.h"
#include "TableInsertTask.h"
#include "TableSelectTask.h"
#include "TableDeleteTask.h"
#include "TableUpdateTask.h"
#include "TableDropTask.h"
#include "sqlCmdDecode.h"
#include "sqlCommandHandleHost.h"
#include "TableManager.h"
#include "ChangeLog.h"
#include "CheckPointTask.h"
#include "MyTool.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_TABLE_HANDLE_TEST
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TABLE_HANDLE_TEST

typedef struct TableHandleTestTaskBitmap_t {
    uint32 done : 1;
    uint32 success : 1;
}TableHandleTestTaskBitmap;

typedef struct TableHandleTestTaskContext_t {
    TableHandleTestTaskState state;
    TableHandleTestTaskBitmap Bitmap;
    TableCreateTaskCmd createCmd;
    TableInsertTaskCmd insertCmd;
    TableSelectTaskCmd selectCmd;
    TableDeleteTaskCmd deleteCmd;
    TableUpdateTaskCmd updateCmd;
    TableDropTaskCmd dropCmd;
    BufferListInfo* load;
    uint32 offset;
    int resp;
}TableHandleTestTaskContext;

static TableHandleTestTaskContext gl_context = { 0 };
static uint64 start = 0;
static uint64 us = 0;

static int callback() {
    int err = addTaskToList(TASK_ID_TABLE_HANDLE_TEST, &gl_context);
    ASSERT(!err, "table handle test, callback fail\n");
    return err == 0;
}

static int init(){
    return initTableMgr();
}

int initTableHandleTestTask(){
    return init();
}

int sqlDecode(char* sql, SqlTokenHandle* cmd) {
    int err = 0;
    char* sqlStream = NULL;
    uint32 sqlStreamLen = 0;
    err = sqlCmdByShell(sql, strlen(sql), &sqlStream, &sqlStreamLen);//453us
    //printm("%lluus\n", myCounterUs(time));//453us
    if (err) {
        printm("make stream fail\n");
        ERROR(-1);
    }
    //showSqlStream(sqlStream, sqlStreamLen); printm("\n");

    err = SqlHandleDecodeStream(sqlStream, sqlStreamLen, cmd); if (err) { printm("decode fail\n"); ERROR(-1); }
    //SqlHandleViewTokens(cmd);

    return 0;
error:
    free(sqlStream);
    return err;
}

int test_shell_input(SqlTokenHandle* cmd) {
    int err = 0;

    char sql[1024] = { 0 };
    //client part
    

    printm("sql$: ");
    keybordInput(sql, 1024);

    if (strncmp("quit", sql, 4) == 0) {
        exit(0);
    }
    else if (sql[0] == 0 || sql[0] == '\n') {
        return -1;
    }

    err = sqlDecode(sql, cmd); if (err) { return - 1; }

    return 0;
}
int test_insert_x10_and_delete_all(SqlTokenHandle* cmd, BufferListInfo* out) {
    int err = 0;
    static uint64 totalTime = 0;
    static uint64 start = 0;
    static uint64 us = 0;

    char sql[1024] = { 0 };
    //client part
    char* sqlStream = NULL;
    uint32 sqlStreamLen = 0;
    typedef enum loc_state_e{
        STATE_START,
        STATE_INSERT,
        STATE_SELECT,
        STATE_DELETE,
        STATR_COMPLETE
    }loc_state;
#define INSERT_CONTINUE_CNT (10)
#define DELETE_CNT_MAX (BYTE_1K*4.0)
    static loc_state state = STATE_START;
    static double insertCnt = 0;
    static double deleteCnt = 0;
    static uint32 id = 0;

    if (state == STATE_START) {
        printm("*** test insert_x10_and_delete_all ***\n");
        hide_cursor();
        state = STATE_INSERT;
    }
    else if (state == STATE_INSERT) {
        state = STATE_SELECT;
    }
    else if (state == STATE_SELECT) {
        //check 
        ASSERT(!out, "respons is null\n");
        ASSERT(out->size != (4 + 16 + 20), "respons len error\n");
        char* data = out->buff;
        uint32* out_id= data;
        char* out_name = out_id + 1;
        char* out_password = out_name + 16;
        ASSERT(*out_id != id ||
            strncmp(out_name, "zbh", 3) != 0 ||
            strncmp(out_password, "123", 3) != 0
            , "inserted data not true\n");

        id++;
        insertCnt++;
        state = STATE_INSERT;
        if (insertCnt >= INSERT_CONTINUE_CNT) {
            state = STATE_DELETE;
            id = 0;
            insertCnt = 0;
        }
    }
    else if (state == STATE_DELETE) {
        print_progress_bar(((deleteCnt) / DELETE_CNT_MAX) * 100.0);
        deleteCnt++;
        state = STATE_INSERT;
        if (deleteCnt >= DELETE_CNT_MAX) {
            state = STATR_COMPLETE;
        }
    }
    

    switch (state) {
    case STATE_INSERT: {
        sprintf(sql, "insert into users (id, name, password) value (%lu, 'zbh', '123')", id);
        break;
    }
    case STATE_SELECT: {
        sprintf(sql, "select * from users where id=%u", id);
        break;
    }
    case STATE_DELETE: {
        sprintf(sql, "delete from users where name='zbh'");
        break;
    }
    case STATR_COMPLETE: {
        print_progress_bar(100);
        printm("\n*** pass ***\n");
        show_cursor();
        exit(0);
        break;
    }
    default: {
    
        ASSERT(TRUE, "invalid state\n");
        exit(0);
    }
    }

    err = sqlDecode(sql, cmd); if (err) { return -1; }

    return 0;
}

//处理客户端的请求
TaskState TableHandleTestTask(void* inP) {
    int err = 0;
    TableHandleTestTaskState* state = &(gl_context.state);
   
    static BufferListInfo* selectOut = NULL;

    //host part
    static SqlTokenHandle cmd = { 0 };
    switch (*state) {
    case TABLE_HANDLE_TEST_TASK_IDLE: {
#if 1
        err = test_shell_input(&cmd);
        if (err) { return TASK_STATE_RESTART; }
#else
        err = test_insert_x10_and_delete_all(&cmd, selectOut);
        if (err) { ASSERT(err, "*** error ***\n"); return TASK_STATE_RESTART; }
#endif
        if (selectOut) {
            freeBuffer(&selectOut);
        }
        switch (cmd.type) {
        case SQL_CMD_CREATE: {
            *state = TABLE_HANDLE_TEST_TASK_CREATE;
            break;
        }
        case SQL_CMD_SELECT: {
            *state = TABLE_HANDLE_TEST_TASK_SELECT;
            break;
        }
        case SQL_CMD_UPDATE: {
            *state = TABLE_HANDLE_TEST_TASK_UPDATE;
            break;
        }
        case SQL_CMD_DELETE: {
            *state = TABLE_HANDLE_TEST_TASK_DELETE;
            break;
        }
        case SQL_CMD_INSERT: {
            *state = TABLE_HANDLE_TEST_TASK_INSERT;
            break;
        }
        case SQL_CMD_DROP: {
            *state = TABLE_HANDLE_TEST_TASK_DROP;
            break;
        }
        case SQL_CMD_SHOW_TABLES: {
            *state = TABLE_HANDLE_TEST_TASK_SHOW_TABLES;
            break;
        }
        case SQL_CMD_SHOW_COLUMNS: {
            *state = TABLE_HANDLE_TEST_TASK_SHOW_COLUMNS;
            break;
        }
        default:
            printm("unkown sql command\n");
            *state = TABLE_HANDLE_TEST_TASK_COMPLETE;
        }
        
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_CREATE: {
        *state = TABLE_HANDLE_TEST_TASK_CREATE;
        gl_context.createCmd.callBack = callback;
        gl_context.createCmd.resp = &gl_context.resp;

        {
            gl_context.createCmd.colInfoList = cmd.columnInfoList;
            gl_context.createCmd.columnCnt = cmd.columnInfoList->size / sizeof(SqlColumnItem);
            gl_context.createCmd.name = cmd.tableName;
            gl_context.createCmd.tableId = cmd.tableId;            
        }

        if (sendMsgToTask(TASK_ID_TABLE_CREATE, &gl_context.createCmd)) {
            *state = TABLE_HANDLE_TEST_TASK_WAIT_CREATE_DONE;
            return TASK_STATE_SUSPEND;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_WAIT_CREATE_DONE: {
        //check response
        err = gl_context.resp;
        if (err) {
            printm("*** fail ***\n");
        }
        else {
            printm("*** pass ***\n");
            cmd.tableId = TableMgrGetTableIdByName(cmd.tableName->buff, cmd.tableName->size);
            ASSERT(cmd.tableId == INVALID_TABLE_ID, "invalid table id\n");
            TableHandle* table = GET_TABLE_HANDLE(cmd.tableId);
            
            err = SqlDecodeSetTableInfo(cmd.tableId, table->info, sizeof(TableInfo) + table->info->colNum * sizeof(SqlColumnItem));
            ASSERT(err, "set decode module table info fail");
            TableMgrViewTableInfo(cmd.tableId);
        }

        *state = TABLE_HANDLE_TEST_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_SELECT: {
        start = myCounterStart();
        cmd.tableId = TableMgrGetTableIdByName(cmd.tableName->buff, cmd.tableName->size);
        err = TableMgrStrColumnListToColumnIdList(cmd.tableId, &cmd.destColList);
        if (err) { ASSERT(err, "strColList to colIdList fail\n"); }
        ASSERT(cmd.tableId == INVALID_TABLE_ID, "invalid table id\n");
        gl_context.selectCmd.callBack = callback;
        gl_context.selectCmd.resp = &gl_context.resp;

        {
            gl_context.selectCmd.out = &gl_context.load;
            memcpy(&gl_context.selectCmd.tk, &cmd, sizeof(cmd));
            gl_context.selectCmd.tableId = cmd.tableId;
        }

        if (sendMsgToTask(TASK_ID_TABLE_SELECT, &gl_context.selectCmd)) {
            *state = TABLE_HANDLE_TEST_TASK_WAIT_SELECT_DONE;
            return TASK_STATE_SUSPEND;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_WAIT_SELECT_DONE: {
        if (gl_context.resp) {            
            if (gl_context.resp > 0) {
                printm("no match row\n");
            }
            else {
                printm("select fail, err[%d]\n", gl_context.resp);
            }
        }
        else {
            if (gl_context.load) {
                selectOut = gl_context.load;
                gl_context.load = NULL;
                //TableMgrViewSelectOutput(gl_context.selectCmd.tk, gl_context.load);
            }
            else {
                ASSERT(TRUE, "select success but no date return?\n");
            }
        }
        
        *state = TABLE_HANDLE_TEST_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_UPDATE: {
        //err = TableMgrStrColumnListToColumnIdList(cmd.tableId, &cmd.destColList);
        //if (err) { ASSERT(err, "strColList to colIdList fail\n"); }
        gl_context.updateCmd.callBack = callback;
        gl_context.updateCmd.resp = &gl_context.resp;

        {
            memcpy(&gl_context.updateCmd.tk, &cmd, sizeof(cmd));
            gl_context.updateCmd.tableId = cmd.tableId;
        }

        if (sendMsgToTask(TASK_ID_TABLE_UPDATE, &gl_context.updateCmd)) {
            *state = TABLE_HANDLE_TEST_TASK_WAIT_UPADTE_DONE;
            return TASK_STATE_SUSPEND;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_WAIT_UPADTE_DONE: {
        if (gl_context.resp) {
            printm("*** update fail ***\n");
        }
        else {
            static uint32 cnt = 0;
            //printm("*** update success ***\n");
        }
        *state = TABLE_HANDLE_TEST_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_DELETE: {
        cmd.tableId = TableMgrGetTableIdByName(cmd.tableName->buff, cmd.tableName->size);
        ASSERT(cmd.tableId == INVALID_TABLE_ID, "invalid table id\n");
        gl_context.deleteCmd.callBack = callback;
        gl_context.deleteCmd.resp = &gl_context.resp;

        {
            gl_context.deleteCmd.tableId = cmd.tableId;
            memcpy(&gl_context.deleteCmd.tk, &cmd, sizeof(cmd));
        }

        if (sendMsgToTask(TASK_ID_TABLE_DELETE, &gl_context.deleteCmd)) {
            *state = TABLE_HANDLE_TEST_TASK_WAIT_DELETE_DONE;
            return TASK_STATE_SUSPEND;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_WAIT_DELETE_DONE: {
        if (gl_context.resp) {
            printm("delete fail\n");
            ASSERT(TRUE, "");
        }
        else {
            //printm("*** delete success ***\n");
        }

        *state = TABLE_HANDLE_TEST_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_INSERT: {
        cmd.tableId = TableMgrGetTableIdByName(cmd.tableName->buff, cmd.tableName->size);
        ASSERT(cmd.tableId == INVALID_TABLE_ID, "invalid table id\n");
        gl_context.insertCmd.callBack = callback;
        gl_context.insertCmd.resp = &gl_context.resp;

        {
            gl_context.insertCmd.columnInfoList = cmd.columnInfoList;
            gl_context.insertCmd.tableId = cmd.tableId;
        }

        if (sendMsgToTask(TASK_ID_TABLE_INSERT, &gl_context.insertCmd)) {
            *state = TABLE_HANDLE_TEST_TASK_WAIT_INSERT_DONE;
            return TASK_STATE_SUSPEND;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_WAIT_INSERT_DONE: {
        err = gl_context.resp;
        if (err) {
            printm("*** insert fail ***\n");
            ASSERT(TRUE, "");
        }
        else {
            //printm("*** insert pass ***\n");
        }

        *state = TABLE_HANDLE_TEST_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_DROP: {
        *state = TABLE_HANDLE_TEST_TASK_CREATE;
        cmd.tableId = TableMgrGetTableIdByName(cmd.tableName->buff, cmd.tableName->size);
        gl_context.dropCmd.callBack = callback;
        gl_context.dropCmd.resp = &gl_context.resp;

        gl_context.dropCmd.tableId = cmd.tableId;

        if (sendMsgToTask(TASK_ID_TABLE_DROP, &gl_context.dropCmd)) {
            *state = TABLE_HANDLE_TEST_TASK_WAIT_DROP_DONE;
            return TASK_STATE_SUSPEND;
        }
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_WAIT_DROP_DONE: {
        err = gl_context.resp;
        if (err) {
            printm("*** drop fail ***\n");
        }
        else {
            //printm("*** drop pass ***\n");
        }
        *state = TABLE_HANDLE_TEST_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_SHOW_TABLES: {
        err = TableMgrStrTableNameListToTableIdList(&cmd.destTableList);
        ASSERT(err, "tableNameList to tableIdList fail when show tables");
        if (cmd.destTableList) {
            err = TableMgrShowTables(cmd.destTableList->buff, cmd.destTableList->size / sizeof(uint64));
        }
        else {
            err = TableMgrShowTables(NULL, 0);
        }

        ASSERT(err, "show tables fail");
        *state = TABLE_HANDLE_TEST_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_SHOW_COLUMNS: {
        uint32 colCnt = 0;
        uint64 tableId = TableMgrGetTableIdByName(cmd.tableName->buff, cmd.tableName->size);
        if (tableId == INVALID_TABLE_ID) {
            printm("invalid table\n");
            break;
        }
        if (cmd.destColList) {
            err = TableMgrStrColumnListToColumnIdList(tableId, &cmd.destColList); ASSERT(err, "strColumnListToColumnIdList fail");
            colCnt = cmd.destColList->size / sizeof(uint64);
        }
        if (cmd.destColList) {
            err = TableMgrShowColumns(tableId, cmd.destColList->buff, colCnt); ASSERT(err, "show columns fail");
        }
        else {
            err = TableMgrShowColumns(tableId, NULL, 0); ASSERT(err, "show columns fail");
        }

        *state = TABLE_HANDLE_TEST_TASK_COMPLETE;
        return TASK_STATE_RESTART;
    }
    case TABLE_HANDLE_TEST_TASK_COMPLETE: {
        *state = TABLE_HANDLE_TEST_TASK_IDLE;
        freeBuffer(&gl_context.load);
        gl_context.offset = 0;
        gl_context.resp = 0;
        SqlHandleFreeTokens(&cmd);
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

bool TableHandleTestTaskMessage(void* parent) {
    int err = 0;

    err = addTaskToList(TASK_ID_TABLE_HANDLE_TEST, parent);   
    return err;
}