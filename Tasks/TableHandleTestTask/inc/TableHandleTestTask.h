#include "MyTypes.h"
#include "TableManager.h"

#define DEBUG_TABLE_HANDLE_TEST

typedef enum TableHandleTestTaskState_e {
	TABLE_HANDLE_TEST_TASK_IDLE,
	TABLE_HANDLE_TEST_TASK_CREATE,
	TABLE_HANDLE_TEST_TASK_WAIT_CREATE_DONE,
	TABLE_HANDLE_TEST_TASK_SELECT,
	TABLE_HANDLE_TEST_TASK_WAIT_SELECT_DONE,
	TABLE_HANDLE_TEST_TASK_UPDATE,
	TABLE_HANDLE_TEST_TASK_WAIT_UPADTE_DONE,
	TABLE_HANDLE_TEST_TASK_DELETE,
	TABLE_HANDLE_TEST_TASK_WAIT_DELETE_DONE,
	TABLE_HANDLE_TEST_TASK_INSERT,
	TABLE_HANDLE_TEST_TASK_WAIT_INSERT_DONE,
	TABLE_HANDLE_TEST_TASK_DROP,
	TABLE_HANDLE_TEST_TASK_WAIT_DROP_DONE,

	TABLE_HANDLE_TEST_TASK_SHOW_TABLES,
	TABLE_HANDLE_TEST_TASK_SHOW_COLUMNS,
	TABLE_HANDLE_TEST_TASK_COMPLETE,
}TableHandleTestTaskState;

typedef struct TableHandleTestTaskCmd_t {
	uint64 tableId;
	char name[TABLE_NAME_LEN_MAX];
	uint32 columnCnt;
	char* colInfoList;
	int* resp;
}TableHandleTestTaskCmd;



int initTableHandleTestTask();
TaskState TableHandleTestTask(void* inP);
bool TableHandleTestTaskMessage(void* parent);