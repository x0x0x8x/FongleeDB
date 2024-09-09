#include "MyTypes.h"
#include "ChangeLog.h"
#include "BufferManager.h"

#define DEBUG_CHANGE_LOG

typedef enum ChangeLogTaskState_e {
	CHANGE_LOG_TASK_IDLE,
	CHANGE_LOG_TASK_PREPARE,
	CHANGE_LOG_TASK_CHECKPOINT_SYNC,
	CHANGE_LOG_TASK_FLUSH_LOG,
	CHANGE_LOG_TASK_WAIT_FLUSH_LOG_DONE,
	CHANGE_LOG_TASK_COMPLETE,
	CHANGE_LOG_TASK_ERROR,
}ChangeLogTaskState;

typedef struct ChangeLogTaskCmd_t {
	logItemType type;
	uint64 fileId;
	uint64 seek;
	BufferListInfo* data;
	uint32 offset;
	uint32 size;
	int* resp;
	TaskDoneCallBack callback;
}ChangeLogTaskCmd;

int initChangeLogTask();
TaskState ChangeLogTask(void* inP);
bool ChangeLogTaskMessage(void* parent);
int ChangeLogTaskCallBack();