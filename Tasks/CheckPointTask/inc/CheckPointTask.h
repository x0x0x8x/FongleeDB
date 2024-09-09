#include "MyTypes.h"

#define DEBUG_CHECK_POINT

typedef enum CheckPointTaskState_e {
	CHECK_POINT_TASK_IDLE,
	CHECK_POINT_TASK_UPDATE_TO_STORAGE,
	CHECK_POINT_TASK_WAIT_UPDATE_TO_STORAGE_DONE,
	CHECK_POINT_TASK_FLUSH_TO_DISK,
	CHECK_POINT_TASK_WAIT_FLUSH_TO_DISK_DONE,
	CHECK_POINT_TASK_CLEAN,
	CHECK_POINT_TASK_COMPLETE,
	CHECK_POINT_TASK_ERROR,
}CheckPointTaskState;

typedef struct CheckPointTaskParam_t {
	int* resp;
	TaskDoneCallBack callback;
}CheckPointTaskParam;

int initCheckPointTask();
TaskState CheckPointTask(void* inP);
bool CheckPointTaskMessage(void* parent);
int CheckPointCallBack();
bool CheckPointTaskIsIdle();