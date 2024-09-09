#include "TaskHandler.h"
#include "MainLoopTask.h"
#include "ChangeLog.h"
#include "ChangeLogTask.h"
#include "ChangeLogTestTask.h"
#include "CheckPointTask.h"
#include "TableCreateTask.h"
#include "TableInsertTask.h"
#include "TableSelectTask.h"
#include "TableSelectRowTask.h"
#include "TableDeleteTask.h"
#include "TableUpdateTask.h"
#include "TableDropTask.h"
#include "TableHandleTestTask.h"
#include "ReadTask.h"
#include "ThreadManager.h"
#include "MyTime.h"
#include <stdlib.h>
#include <stdio.h>


#ifdef DEBUG_ON
#define ASSERT(condition,...) assert_gl(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TASK_HANDLER

#define IS_INVALID_TASK_ID(id) (id >= TASK_ID_NUM)
#define IS_INVALID_TASK_QUEUE_LEVEL(level) (level >= LEVEL_NUM || level < 0)

#define MAX_TASK_QUEUE_CNT (50)
typedef struct TaskBitMap_t {
	uint32 isInQueue : 1;
	uint32 isMultiTask : 1;//multi same task could in queue
	uint32 multiCnt : 8;
}TaskBitMap;
typedef struct TaskInfo_t {
	const TaskId Id;
	const char* name;
	const TaskPointer task;//task main function ptr
	const TaskMsgPointer msg;//task message function ptr
	TaskDoneCallBack callBack;
	TaskState state;
	const TaskQueueLevel level;
	TaskBitMap BitMap;
}TaskInfo;
typedef enum TaskHandlerState_e {
	STATE_IDLE,
	STATE_NORMAL,
	STATE_SLEEP,
}TaskHandlerState;

typedef struct TaskContext_t {
	TaskId id;
	void* context;//dest task's context
}TaskContext;

typedef struct TaskQueue_t {
	TaskContext qList[MAX_TASK_QUEUE_CNT];
	uint32 qSize;//valid len
	uint32 head;
	uint32 tail;
}TaskQueue;

static TaskHandlerState gl_state = STATE_IDLE;

static TaskInfo taskInfo[TASK_ID_NUM] = {
	//task id				task name		task主入口			task消息接收入口			//回调					Task状态机 			优先级		Bitmap	
	{TASK_ID_MAIN_LOOP,		"Main",			MainLoopTask,		NULL,						NULL,					TASK_STATE_IDLE,	UPPER,		0},
	{TASK_ID_CHANGE_LOG,	"ChangeLog",	ChangeLogTask,		ChangeLogTaskMessage,		ChangeLogTaskCallBack,	TASK_STATE_IDLE,	UPPER,		0},
	{TASK_ID_CHECK_POINT,	"CheckPoint",	CheckPointTask,		CheckPointTaskMessage,		CheckPointCallBack,		TASK_STATE_IDLE,	LOWER,		0},
	{TASK_ID_READ,			"Read",			ReadTask,			ReadTaskMessage,			ReadTaskCallBack,		TASK_STATE_IDLE,	LOWER,		.BitMap.isMultiTask = 1},
	{TASK_ID_TABLE_CREATE,	"TableCreate",	TableCreateTask,	TableCreateTaskMessage,		TableCreateCallBack,	TASK_STATE_IDLE,	UPPER,		.BitMap.isMultiTask = 1},
	{TASK_ID_TABLE_INSERT,	"TableInsert",	TableInsertTask,	TableInsertTaskMessage,		TableInsertCallBack,	TASK_STATE_IDLE,	UPPER,		.BitMap.isMultiTask = 1},
	{TASK_ID_TABLE_SELECT,	"TableInsert",	TableSelectTask,	TableSelectTaskMessage,		TableSelectCallBack,	TASK_STATE_IDLE,	UPPER,		.BitMap.isMultiTask = 1},
	{TASK_ID_TABLE_SELECT_ROW,"TableInsert",TableSelectRowTask,	TableSelectRowTaskMessage,	TableSelectRowCallBack,	TASK_STATE_IDLE,	UPPER,		.BitMap.isMultiTask = 1},
	{TASK_ID_TABLE_DELETE,	"TableDelete",	TableDeleteTask,	TableDeleteTaskMessage,		TableDeleteCallBack,	TASK_STATE_IDLE,	UPPER,		.BitMap.isMultiTask = 1},
	{TASK_ID_TABLE_UPDATE,	"TableUpdate",	TableUpdateTask,	TableUpdateTaskMessage,		TableUpdateCallBack,	TASK_STATE_IDLE,	UPPER,		.BitMap.isMultiTask = 1},
	{TASK_ID_TABLE_DROP,	"TableDrop",	TableDropTask,		TableDropTaskMessage,		TableDropCallBack,		TASK_STATE_IDLE,	UPPER,		.BitMap.isMultiTask = 1},
	
	{TASK_ID_CHANGE_LOG_TEST,	"ChangeLogTest",	ChangeLogTestTask,		ChangeLogTestTaskMessage,	NULL,		TASK_STATE_IDLE,	UPPER,		0},
	{TASK_ID_TABLE_HANDLE_TEST,	"TableHandleTest",	TableHandleTestTask,	TableHandleTestTaskMessage,	NULL,		TASK_STATE_IDLE,	UPPER,		0},
};

void assert_gl(bool condition, const char* format, ...);

static TaskQueue gl_TaskQ[LEVEL_NUM] = {0};
typedef struct TestLogFlag_t {
	volatile uint8 state : 3;
	volatile uint8 isReading : 1;
}TestLogFlag;

void initTaskHandler() {
	static uint8 inited = 0;
	if (inited)
		return;
	int err = 0;
	//buffer manager
#ifdef BUFFER_MANAGER
	err = initBufferManagserBySize(BUFFER_POOL_MAX, BUFFER_POOL_NORMAL, BYTE_1G); if (err < 0) { printm("init sow fail\n"); exit(0); }
#endif // BUFFER_MANAGER

#ifdef THREAD_POOL
	err = initThreadManager(); ASSERT_GL(err < 0, "init thread fail\n"); if (err < 0) { printm("init thread pool fail\n"); exit(0); }
#endif // THREAD_POOL
	err = initChangeLogTask();	if (err < 0) { printm("init ChangeLogTask fail\n"); exit(0); }
	err = initTableCreateTask(); if (err < 0) { printm("init TableCreateTask fail\n"); exit(0); }
	err = initTableHandleTestTask(); if (err < 0) { printm("init TableHandleTestTask fail\n"); exit(0); }

	for (uint8 i = 0; i < LEVEL_NUM; i++) {
		for (uint8 j = 0; j < MAX_TASK_QUEUE_CNT; j++) {
			gl_TaskQ[i].qList[j].id = TASK_ID_FREE;
		}
	}

	addTaskToList(TASK_ID_MAIN_LOOP, NULL);

	inited = 1;
}
int pushQ(TaskId id, TaskQueueLevel level, void* context) {
#define QUEUE_INDEX_CREATE(index) (index = (index+1)%MAX_TASK_QUEUE_CNT)
	if (IS_INVALID_TASK_ID(id) || IS_INVALID_TASK_QUEUE_LEVEL(level)) { return -1; }
	if (gl_TaskQ[level].qSize >= MAX_TASK_QUEUE_CNT) { return -1; }
	TaskQueue* q = &(gl_TaskQ[level]);
	if (q->qSize == 0) {
		q->head = 0;
		q->tail = 1;
		q->qList[q->head].id = id;
		q->qList[q->head].context = context;
	}
	else {
#ifdef TASK_DEBUG
		if (q->qList[q->tail].id != TASK_ID_FREE) {
			ASSERT("pushQ tail not free");
		}
#endif // DEBUG
		q->qList[q->tail].id = id;
		q->qList[q->tail].context = context;
		QUEUE_INDEX_CREATE(q->tail);//tail++
	}

	q->qSize++;
	return 0;
}
int popQ(TaskQueueLevel level, TaskId* id, void** context) {
	ASSERT(IS_INVALID_TASK_QUEUE_LEVEL(level) || !id || !context, "invalid input")

	if (gl_TaskQ[level].qSize == 0) { return -1; }
	TaskId ret = TASK_ID_FREE;
	TaskQueue* q = &(gl_TaskQ[level]);

	ASSERT(q->qList[q->head].id == TASK_ID_FREE,"popQ head is free") 

	*id = q->qList[q->head].id;
	*context = q->qList[q->head].context;

	q->qList[q->head].id = TASK_ID_FREE;
	QUEUE_INDEX_CREATE(q->head);
	q->qSize--;
	return 0;
}
void taskLoop() {
	static time_t lastFreeStateTime = 0;
	static TaskQueueLevel level;
	static uint8 upperCnt = 0;
	if (upperCnt >= TASK_QUEUE_UPPER_FIRST_CNT) {
		level = LOWER;
		upperCnt = 0;
	}
	else {
		level = UPPER;
		upperCnt++;
	}

	TaskId id;
	void* context = 0;
	//只有两个Q
	int err = popQ(level, &id, &context);
	if (err < 0) {
		level = (level + 1) % LEVEL_NUM;
		err = popQ(level, &id, &context);
	}

	if (err < 0) {
		//no task
		if (lastFreeStateTime == 0)
			lastFreeStateTime = getCurTick();
		else {
			if (getCurTick() - lastFreeStateTime > TASK_HANDLER_IDLE_TIME) {
				gl_state = STATE_NORMAL;
			}
		}

		return;
	}

	if (taskInfo[id].BitMap.isMultiTask) {
		taskInfo[id].BitMap.multiCnt--;
		if (taskInfo[id].BitMap.multiCnt == 0) {
			taskInfo[id].BitMap.isInQueue = FALSE;
		}
	}
	else {
		taskInfo[id].BitMap.isInQueue = FALSE;
	}
	//if (taskInfo[id].state == TASK_STATE_SUSPEND) {
	//	ASSERT(!addTaskToList(id, context), "task insert fail")
	//	return;
	//}
	//run task
	lastFreeStateTime = 0;	gl_state = STATE_NORMAL;
	TaskPointer curTask = taskInfo[id].task;
	ASSERT(curTask == NULL,"current task is null!!\n");
	TaskDoneCallBack callBack = taskInfo[id].callBack;
	
	//run
	TaskState ret = (*curTask)(context);
	taskInfo[id].state = ret;
	switch (ret)
	{
	case TASK_STATE_RESTART:
		ASSERT(!addTaskToList(id, context),"task restart fail")
		break;
	case TASK_STATE_SUSPEND:
		break;
	case TASK_STATE_COMPLETE:
		if (callBack) {
			if ((*callBack)()) {
				printm("callback run fail!!!\n");
				ASSERT(TRUE, "callback run fail!!!\n");
			}
		}
		taskInfo[id].state = TASK_STATE_IDLE;
		break;
	default:
		break;
	}

	return;
}
void taskMainBoot() {
	initTaskHandler();
	printm("task handler running\n\n");
	
	while (TRUE) {
		taskLoop();
	}
}
bool TaskCanAddSuccess(TaskId id) {
	TaskQueueLevel level = taskInfo[id].level;
	if (IS_INVALID_TASK_ID(id) || IS_INVALID_TASK_ID(id) || IS_INVALID_TASK_QUEUE_LEVEL(level) || gl_TaskQ[level].qSize >= MAX_TASK_QUEUE_CNT) {
		return FALSE;
	}
	else {
		return TRUE;
	}
}
bool addTaskToList(TaskId id, void* context) {
#ifdef DEBUG_ON
	if (IS_INVALID_TASK_ID(id)) { return FALSE; }
#endif // DEBUG_ON
	TaskQueueLevel level = taskInfo[id].level;
	if (taskInfo[id].BitMap.isInQueue && !taskInfo[id].BitMap.isMultiTask) {
		//alraedy in task queue
		return TRUE;
	}
	if (taskInfo[id].BitMap.isMultiTask) {
		taskInfo[id].BitMap.multiCnt++;
	}
	int err = pushQ(id, level, context); if (err < 0) { return FALSE; }
	taskInfo[id].BitMap.isInQueue = TRUE;

	return TRUE;
}
bool sendMsgToTask(TaskId id, void* parentIndex) {
	if (id == TASK_ID_FREE)
		return FALSE;
	
	return (*(taskInfo[id].msg))(parentIndex);
}
