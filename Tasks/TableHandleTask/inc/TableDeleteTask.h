#pragma once
#include "MyTypes.h"
#include "TableManager.h"

#define DEBUG_TABLE_DELETE

typedef struct TableDeleteTaskCmd_t {
	uint64 tableId;
	SqlTokenHandle tk;
	TaskDoneCallBack callBack;
	int* resp;
}TableDeleteTaskCmd;

int initTableDeleteTask();
TaskState TableDeleteTask(void* inP);
bool TableDeleteTaskMessage(void* parent);
int TableDeleteCallBack();