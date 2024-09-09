#pragma once
#include "MyTypes.h"
#include "TableManager.h"

#define DEBUG_TABLE_DROP


typedef struct TableDropTaskCmd_t {
	uint64 tableId;
	TaskDoneCallBack callBack;
	int* resp;
}TableDropTaskCmd;

int initTableDropTask();
TaskState TableDropTask(void* inP);
bool TableDropTaskMessage(void* parent);
int TableDropCallBack();