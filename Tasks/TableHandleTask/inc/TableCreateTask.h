#pragma once
#include "MyTypes.h"
#include "TableManager.h"

#define DEBUG_TABLE_CREATE


typedef struct TableCreateTaskCmd_t {
	uint64 tableId;
	BufferListInfo* name;
	uint32 columnCnt;
	BufferListInfo* colInfoList;
	TaskDoneCallBack callBack;
	int* resp;
}TableCreateTaskCmd;

int initTableCreateTask();
TaskState TableCreateTask(void* inP);
bool TableCreateTaskMessage(void* parent);
int TableCreateCallBack();