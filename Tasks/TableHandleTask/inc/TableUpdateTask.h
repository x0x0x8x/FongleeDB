#pragma once
#include "MyTypes.h"
#include "TableManager.h"

#define DEBUG_TABLE_UPDATE

//static int selectRowList_ScanTable(uint64 tableId, SqlTokenHandle tk, BufferListInfo** out)
typedef struct TableUpdateTaskCmd_t {
	uint64 tableId;
	SqlTokenHandle tk;
	TaskDoneCallBack callBack;
	int* resp;
}TableUpdateTaskCmd;

int initTableUpdateTask();
TaskState TableUpdateTask(void* inP);
bool TableUpdateTaskMessage(void* parent);
int TableUpdateCallBack();