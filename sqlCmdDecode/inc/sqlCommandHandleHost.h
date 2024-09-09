#pragma once
#include "BufferManager.h"
#include "sqlTypes.h"
#include "operatorTypeDef.h"

#define DEBUG_SQL_DECODE_HOST

#define BUFFER_POOL (BUFFER_POOL_NORMAL)

typedef int (*SqlHandleGetColumnValueByNameCB)(uint64 tableId, char* row, char* columnName, uint32 len, BufferListInfo** outValue);
typedef struct SqlTokenHandle_t {
	SqlCmdType type;
	uint64 tableId;
	BufferListInfo* tableName;
	union {
		BufferListInfo* destTableList;//for show tables
		BufferListInfo* destColList;//for select
		BufferListInfo* setList;//for update
		BufferListInfo* columnInfoList;//for create,insert	//SqlColumnItem[] //[SqlInsertColumnInfo, value]...
	};
	BufferListInfo* whereStack;//stackNode
}SqlTokenHandle;

int SqlHandleDecodeStream(char* stream, uint64 size, SqlTokenHandle* tk);
void SqlHandleViewTokens(SqlTokenHandle* tk);
void SqlHandleFreeTokens(SqlTokenHandle* tk);
bool SqlHandleCalculateExpression(uint64 tableId, char* row, BufferListInfo** exp, SqlHandleGetColumnValueByNameCB callback);
int SqlHandleCalculateSetExpression(uint64 tableId, char* row, BufferListInfo** exp, BufferListInfo** destCol, BufferListInfo** value, SqlHandleGetColumnValueByNameCB callback);
void SqlHandleViewExpStack(BufferListInfo* head);

void sqlCommandHandle_test1();
void sqlCommandHandle_test2();