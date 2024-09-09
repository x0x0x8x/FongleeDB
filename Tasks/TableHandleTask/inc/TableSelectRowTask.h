#pragma once
#include "MyTypes.h"
#include "TableManager.h"

#define DEBUG_TABLE_SELECT_ROW

//static int selectRowList_ScanTable(uint64 tableId, SqlTokenHandle tk, BufferListInfo** out)
typedef struct TableSelectRowTaskCmd_t {
	uint64 tableId;
	uint64 rowId;
	BufferListInfo** row;
	uint32* offset;
	TaskDoneCallBack callBack;
	int* resp;
}TableSelectRowTaskCmd;

int initTableSelectRowTask();
TaskState TableSelectRowTask(void* inP);
bool TableSelectRowTaskMessage(void* parent);
int TableSelectRowCallBack();