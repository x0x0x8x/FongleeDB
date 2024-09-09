#pragma once
#include "MyTypes.h"
#include "MyTool.h"

#define BUFFER_MAX_SIZE_GLOBL (1*BYTE_1G)//buffer pool size

#define TASK_QUEUE_UPPER_FIRST_CNT (2) //高优先级执行几次后执行一次低优先级任务
#define TASK_HANDLER_IDLE_TIME (60) //无任务等待几秒后开启空闲状态

typedef enum TaskQueueLevel_e {
	UPPER,
	LOWER,
	LEVEL_NUM
}TaskQueueLevel;

typedef enum TaskState_e {
	TASK_STATE_IDLE = 0,
	TASK_STATE_RESTART,
	TASK_STATE_SUSPEND,
	TASK_STATE_COMPLETE
}TaskState;

typedef enum TaskId_e {
	TASK_ID_MAIN_LOOP,
	TASK_ID_CHANGE_LOG,
	TASK_ID_CHECK_POINT,
	TASK_ID_READ,
	TASK_ID_TABLE_CREATE,
	TASK_ID_TABLE_INSERT,
	TASK_ID_TABLE_SELECT,
	TASK_ID_TABLE_SELECT_ROW,
	TASK_ID_TABLE_DELETE,
	TASK_ID_TABLE_UPDATE,
	TASK_ID_TABLE_DROP,

	TASK_ID_CHANGE_LOG_TEST,
	TASK_ID_TABLE_HANDLE_TEST,
	
	TASK_ID_NUM,
	TASK_ID_FREE = TASK_ID_NUM,
}TaskId;

typedef TaskState (*TaskPointer)(void* context);
typedef bool (*TaskMsgPointer)(void* context);
typedef int (*TaskDoneCallBack)();

void taskMainBoot();
bool TaskCanAddSuccess(TaskId id);
bool addTaskToList(TaskId id, void* context);
bool sendMsgToTask(TaskId id, void* context);


