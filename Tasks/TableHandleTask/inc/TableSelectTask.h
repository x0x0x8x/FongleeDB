#pragma once
#include "MyTypes.h"
#include "TableManager.h"

#define DEBUG_TABLE_SELECT

//static int selectRowList_ScanTable(uint64 tableId, SqlTokenHandle tk, BufferListInfo** out)
typedef struct TableSelectTaskCmd_t {
	uint64 tableId;
	SqlTokenHandle tk;
	BufferListInfo** out;
	TaskDoneCallBack callBack;
	int* resp;
}TableSelectTaskCmd;

int initTableSelectTask();
TaskState TableSelectTask(void* inP);
bool TableSelectTaskMessage(void* parent);
int TableSelectCallBack();