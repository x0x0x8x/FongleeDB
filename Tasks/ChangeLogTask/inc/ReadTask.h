#include "MyTypes.h"
#include "BufferManager.h"

#define DEBUG_READ

#define BUFFER_POOL BUFFER_POOL_NORMAL

typedef enum ReadTaskState_e {
	READ_TASK_IDLE,
	READ_TASK_READ_FROM_BLOG,
	READ_TASK_WAIT_READ_FROM_BLOG_DONE,
	READ_TASK_READ_FROM_FLOG,
	READ_TASK_WAIT_READ_FROM_FLOG_DONE,
	READ_TASK_CHECKPOINT,
	READ_TASK_WAIT_CHECKPOINT_DONE,
	READ_TASK_READ_FROM_STORAGE,
	READ_TASK_WAIT_READ_FROM_STORAGE_DONE,
	READ_TASK_COMPLETE,
	READ_TASK_ERROR,
}ReadTaskState;

typedef struct ReadTaskCmd_t {
	uint64 fileId;
	uint64 seek;
	BufferListInfo** data;
	uint32* offset;
	uint32 size;
	int* resp;
	TaskDoneCallBack callback;
}ReadTaskCmd;

int initReadTask();
TaskState ReadTask(void* inP);
bool ReadTaskMessage(void* parent);
int ReadTaskCallBack();