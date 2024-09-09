#pragma once
#include <stdint.h>
#include "sqlCmdDecode.h"
#include "sqlTypes.h"

int removeChrs(char* str, uint32_t size, char* destChr, uint32_t destSize, uint32_t* newLen);

int sqlCmdGetColSize(SqlColumnItem* info);
void sqlCmdViewColumnInfo(SqlColumnItem* info);
int sqlCmd(char* sql, uint32_t sqlLen, char** stream, uint32_t* streamSize, uint32_t dataCnt, ...);
int sqlCmdByShell(char* sql, uint32_t sqlLen, char** stream, uint32_t* streamSize);
void showSqlStream(char* stream, uint32_t size);
int SqlDecodeSetTableInfo(uint64 tableId, char* src, uint64 size);
int colInfoItem2Str(SqlColumnItem* info);
bool isValidTableName(char* name, uint32 len);
uint32 sqlCmdGetColumnValueStrLen(char* value, uint32 size, sqlDataType type);

void debug_sqlCmdByShell();
void sqlCmd_test_charLen();