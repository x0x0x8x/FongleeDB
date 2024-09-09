#pragma once
#include "MyTypes.h"
#include "TableManager.h"

#define DEBUG_TABLE_INSERT

//int TableMgrInsertRow(uint64 tableId, SqlInsertColumnInfo* colList, uint64 len)
typedef struct TableInsertTaskCmd_t {
	uint64 tableId;
	BufferListInfo* columnInfoList;
	TaskDoneCallBack callBack;
	int* resp;
}TableInsertTaskCmd;

int initTableInsertTask();
TaskState TableInsertTask(void* inP);
bool TableInsertTaskMessage(void* parent);
int TableInsertCallBack();