#pragma once
#include "MyTypes.h"
#include "BufferManager.h"
#include "sqlCommandHandleHost.h"
#include "sqlCmdDecode.h"
#include "sqlTypes.h"

#define DEBUG_TABLE_MANAGER
#define BUFFER_POOL (BUFFER_POOL_NORMAL)

#define TABLE_COMMAND_MAX (CPU_CORE_NUM_MAX*2)
#define INVALID_TABLE_ID (U64_MAX)
#define TABLE_CNT_MAX (2048U)
#define TABLE_ID_START_IN_FILE_ID (20U)	
#define TABLE_NAME_LEN_MAX (64U)
#define COLUMN_NAME_LEN_MAX (64U)
#define TABLE_RVT_BLOCK_SIZE (32U)
#define TABLE_RCLOCK_BLOCK_SIZE (32U)
#define TABLE_ROW_BLOOM_FILTER_SIZE (sizeof(uint8)*(32*TABLE_COMMAND_MAX))	//m/n = 32

#define GET_TABLE_FILE_ID(tableId) (tableId + TABLE_ID_START_IN_FILE_ID)
#define GET_TABLE_INFO_FILE_ID(tableId) (GET_TABLE_FILE_ID(tableId) + 1024)
#define GET_TABLE_HANDLE(tableId) (TableMgrGetTableList(tableId))
#define GET_SEEK_BY_ROW_ID(rowId, rowSize) (rowId*(sizeof(RowTag) + rowSize))
#define INVALID_COLUMN_ID (U64_MAX)
#define INVALID_ROW_ID (U64_MAX)

typedef struct RowTag_t {
	uint32 isValid : 1;

}RowTag;
typedef struct TableFlag_t {
	uint32 isValid : 1;
	uint32 isDroping : 1;
}TableFlag;

typedef struct TableHandle_t {
	//id
	volatile TableFlag flag;
	BufferListInfo* infoCache;//table info total values
	TableInfo* info;//valid info data
	BufferListInfo* columnMaxStrLen;

	uint64 rowSize;
	uint64 freeRowId;
	BufferListInfo* rvt;
	BufferListInfo* rclock;
	BufferListInfo* rowBloomFilter;//TABLE_ROW_BLOOM_FILTER_SIZE
	uint64 rowNum;
	uint64 maxValidRowId;
	uint64 userNum;
}TableHandle;

int initTableMgr();
int TableMgrInitRVT(uint64 tableId);
uint64 TableMgrGetFreeTableId();
int TableMgrLoadTableInfo(uint64 tableId);
int TableMgrCreateTable(uint64 tableId, char name[TABLE_NAME_LEN_MAX], uint32 columnCnt, char* colInfoList);
int TableMgrDeleteTable(uint64 tableId);
void TableMgrCleanTable(uint64 tableId);
int TableMgrSetRCLock(uint64 tableId, uint64 rowId, uint64 colId);
int TableMgrSetRowLock(uint64 tableId, uint64 rowId);
bool TableMgrCheckRowLock(uint64 tableId, uint64 rowId);
void TableMgrCleanRCLock(uint64 tableId, uint64 rowId, uint64 colId);
bool TableMgrCheckRCLock(uint64 tableId, uint64 rowId, uint64 colId);
void TableMgrCleanRowLock(uint64 tableId, uint64 rowId);
int TableMgrCreateRow(uint64 tableId, char* dataList, uint64* sizeList);
int TableMgrDeleteRow(uint64 tableId, uint64 rowId);
int TableMgrUpdateColumn(uint64 tableId, uint64 rowId, uint64 colId, char* data, uint64 size);
int TableMgrUpdateColumnToRowBuffer(uint64 tableId, char* row, uint64 colId, char* data, uint64 size);
int TableMgrGetDestRowIdListForUpdate(uint64 tableId, SqlTokenHandle tk, BufferListInfo** rowList);
int TableMgrShowTables(uint64* tableId, uint32 cnt);
int TableMgrShowColumns(uint64 tableId, uint64* colList, uint32 cnt);

TableHandle* TableMgrGetTableList(uint64 tableId);
uint64 TableMgrFindNextFreeRow(uint64 tableId, uint64 startIndex);
int TableMgrSetRVT(uint64 tableId, uint64 rowId);
void TableMgrCleanRVT(uint64 tableId, uint64 rowId);
bool TableMgrCheckRVT(uint64 tableId, uint64 rowId);
uint64 TableMgrGetMaxValidRowIdByRVT(uint64 tableId);
uint64 TableMgrGetNextValidRowIdByRVT(uint64 tableId, uint64 rowId);
uint64 TableMgrGetColIdByName(uint64 tableId, char* columnName, uint32 len);
char* TableMgrGetColValuePtrById(uint64 tableId, char* row, uint64 colId);
uint64 TableMgrGetTableIdByName(char* name, uint32 len);
uint64 TableMgrGetColumnIdListTotalValueSize(uint64 tableId, uint64* colIdList, uint32 cnt);
int TableMgrGetColumnValueByNameCB(uint64 tableId, char* row, char* columnName, uint32 len, BufferListInfo** outValue);
int TableMgrStrTableNameListToTableIdList(BufferListInfo** strTableList);
int TableMgrStrColumnListToColumnIdList(uint64 tableId, BufferListInfo** strColList);
int TableMgrInitRowBloomFilterByTable(uint64 tableId);
int TableMgrSetRowBloomFilter(uint64 tableId, uint64 rowId);
void TableMgrCleanRowBloomFilter(uint64 tableId, uint64 rowId);
bool TableMgrCheckRowBloomFilter(uint64 tableId, uint64 rowId);

void TableMgrViewTableNameList(uint64 tableId);
void TableMgrViewTableInfo(uint64 tableId);
void TableMgrViewRVT(uint64 tableId);
void TableMgrViewTotalTable(uint64 tableId);
void TableMgrViewColumn(uint64 tableId, uint64 columnId, char* value);
void TableMgrViewRow(uint64 tableId, char* data);
void TableMgrViewSelectOutput(SqlTokenHandle tk, BufferListInfo* output);
void TableMgrViewTableList(uint64* tableId, uint32 cnt);

void tableManager_test1();