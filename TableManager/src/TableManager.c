#include "TableManager.h"
#include "storageEngine.h"
#include "ChangeLog.h"

#include "MyStrTool.h"
#include "MyTool.h"
#include "MyTime.h"
#include "Hash.h"


#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_TABLE_MANAGER
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TABLE_MANAGER




const char* gl_dataTypeStr[SQL_DATA_MAX] = {
	"invalid",
	"int8",
	"int16",
	"int32",
	"int64",
	"tableId",
	"float",
	"double",
	"string",
	"image",
	"binary"
};
const int gl_dataTypeSize[SQL_DATA_MAX] = { -1, 1, 2, 4,8,4,4,8,0,0,0 };
extern uint64 gl_fileReadCnt;
extern uint64 gl_fileWriteCnt;

static void viewRVT(uint64 tableId);
static uint64 getColumnIdListTotalValueSize(uint64 tableId, uint64* colIdList, uint32 cnt);
static void viewRow(uint64 tableId, char* data);
static uint64 getTableIdByName(char* name, uint32 len);
static int initMaxStrLen_ScanTable(uint64 tableId);


static TableHandle gl_tableList[TABLE_CNT_MAX] = {0};

static TableHandle* getTableList(uint64 tableId) {
	return gl_tableList + tableId;
}
static uint64 getFreeTableId() {
	uint64 id = 0;
	for (id = 0; id < TABLE_CNT_MAX;id++) {
		if (!gl_tableList[id].info) {
			gl_tableList[id].flag.isValid = TRUE;
			return id;
		}
	}

	return INVALID_TABLE_ID;
}
static uint64 findNextFreeRow(uint64 tableId, uint64 startIndex) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 cnt = (table->rvt->size * 8);
	for (uint64 i = startIndex;i < cnt;i++) {
		if (!checkBitArrayByIndex(table->rvt->buff, i)) {
			ASSERT(TableMgrCheckRVT(tableId, i), "not free row id\n");
			return i;
		}
	}
	
	return MAX(cnt, startIndex);
}
static int initRVT(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	BuffRelease(&table->rvt);
	
	uint64 tableTotalSize = StorageGetFileSize(GET_TABLE_FILE_ID(tableId));
	
	uint64 rowNum = tableTotalSize / (sizeof(RowTag) + table->rowSize);
	uint64 rvtSize = ROUND_UP(rowNum/8+1, TABLE_RVT_BLOCK_SIZE);	
	table->rvt = getBuffer(rvtSize, BUFFER_POOL);	if (!table->rvt) { ERROR(-2); }
	table->freeRowId = U64_MAX;
	table->rowNum = 0;
	for (uint64 i = 0; i < rowNum;i++) {
		uint64 seek = i * (sizeof(RowTag) + table->rowSize);
		BufferListInfo* load = NULL;
		uint64 offset = 0;
		err = StorageRead(GET_TABLE_FILE_ID(tableId), seek, sizeof(RowTag), &load, &offset);	if (err) { ERROR(-3); }
		RowTag* tag = load->buff+ offset;
		if (tag->isValid) {
			setBitArrayByIndex(table->rvt->buff, i);
			table->rowNum++;
			table->maxValidRowId = i;
		}
		else {
			if(table->freeRowId == U64_MAX)
				table->freeRowId = i;
		}
		BuffRelease(&load);
	}
	if (table->freeRowId == U64_MAX) { table->freeRowId = rowNum; }
	if (table->rowNum == 0) { table->maxValidRowId = U64_MAX; }
	//viewRVT(tableId);
	return 0;
error:
	ASSERT(TRUE, "init RVT fail[%d]", err);
	BuffRelease(&table->rvt);
	return -2;
}
static int extendRCLock(uint64 tableId, uint64 maxRCNum) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 destLen = MAX(ROUND_UP(maxRCNum / 8 + 1, TABLE_RCLOCK_BLOCK_SIZE), TABLE_RCLOCK_BLOCK_SIZE);
	if (!table->rclock) {
		table->rclock = getBuffer(destLen, BUFFER_POOL); if (!table->rclock) { return -2; }
	}
	else {
		err = BuffRealloc(&table->rclock, destLen, BUFFER_POOL); if (err) { return -2; }
	}

	return 0;
}
static int initRCLock(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	BuffRelease(&table->rclock);
	TABLE_RCLOCK_BLOCK_SIZE;
	uint64 tableTotalSize = StorageGetFileSize(GET_TABLE_FILE_ID(tableId));
	ASSERT(tableTotalSize % (sizeof(RowTag) + table->rowSize) > 0, "invalid table file size");
	uint64 rowNum = tableTotalSize / (sizeof(RowTag) + table->rowSize);
	uint64 rclockSize = ROUND_UP((rowNum * table->info->colNum) / 8, TABLE_RCLOCK_BLOCK_SIZE);
	table->rclock = getBuffer(rclockSize, BUFFER_POOL);
	if (!table->rclock) { return -2; }

	return 0;
}
static int initRowBloomFilterByTable(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	BuffRelease(&table->rowBloomFilter);
	if (TABLE_ROW_BLOOM_FILTER_SIZE == 0) {
		ASSERT(TABLE_ROW_BLOOM_FILTER_SIZE == 0, "TABLE_ROW_BLOOM_FILTER_SIZE is 0!\n")
		return -1;
	}
	table->rowBloomFilter = getBuffer(TABLE_ROW_BLOOM_FILTER_SIZE, BUFFER_POOL);
	if (!table->rowBloomFilter) { return -2; }
	return 0;
}
static int setRowBloomFilter(uint64 tableId, uint64 rowId) {
	int err = 0;
	ASSERT(tableId == INVALID_TABLE_ID, "invalid tableId");
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (!table->rowBloomFilter) {
		err = initRowBloomFilterByTable(tableId); if (err) { return -2; }
	}

	ASSERT(!table->rowBloomFilter, "row bloom filter not inited\n");
	uint8* ptr = table->rowBloomFilter->buff;
	//uint64 start = myCounterStart();
	uint32 maxLen = table->rowBloomFilter->size / sizeof(uint8);
	uint32 index1 = HashMurmur3_32(&rowId, sizeof(rowId), 0) % maxLen;		if (ptr[index1] == U8_MAX) { return -3; }
	uint32 index2 = HashXXHash_32(&rowId, sizeof(rowId), 0) % maxLen;		if (ptr[index2] == U8_MAX) { return -3; }
	uint32 index3 = HashFnvHash(&rowId, sizeof(rowId)) % maxLen;			if (ptr[index3] == U8_MAX) { return -3; }
	uint32 index4 = HashDjb2Hash(&rowId, sizeof(rowId)) % maxLen;			if (ptr[index4] == U8_MAX) { return -3; }

	ptr[index1]++;
	ptr[index2]++;
	ptr[index3]++;
	ptr[index4]++;
	//uint64 us = myCounterUs(start);
	//printm("set rowBloomFilter %lluus\n", us);	//1us
	return 0;
}
static void cleanRowBloomFilter(uint64 tableId, uint64 rowId) {
	int err = 0;
	ASSERT(tableId == INVALID_TABLE_ID, "invalid tableId");
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	ASSERT(!table->rowBloomFilter, "row bloom filter not inited\n");
	uint8* ptr = table->rowBloomFilter->buff;
	//uint64 start = myCounterStart();
	uint32 maxLen = table->rowBloomFilter->size / sizeof(uint8);
	uint32 index1 = HashMurmur3_32(&rowId, sizeof(rowId), 0) % maxLen;		if (ptr[index1] == 0) { ASSERT(TRUE, "invalid bloom filter slot!"); }
	uint32 index2 = HashXXHash_32(&rowId, sizeof(rowId), 0) % maxLen;		if (ptr[index2] == 0) { ASSERT(TRUE, "invalid bloom filter slot!"); }
	uint32 index3 = HashFnvHash(&rowId, sizeof(rowId)) % maxLen;			if (ptr[index3] == 0) { ASSERT(TRUE, "invalid bloom filter slot!"); }
	uint32 index4 = HashDjb2Hash(&rowId, sizeof(rowId)) % maxLen;			if (ptr[index4] == 0) { ASSERT(TRUE, "invalid bloom filter slot!"); }

	ptr[index1]--;
	ptr[index2]--;
	ptr[index3]--;
	ptr[index4]--;
	//uint64 us = myCounterUs(start);
	//printm("clean rowBloomFilter %lluus\n", us);	//1us
	return 0;
}
static bool checkRowBloomFilter(uint64 tableId, uint64 rowId) {
	int err = 0;
	bool isLock = FALSE;
	ASSERT(tableId == INVALID_TABLE_ID, "invalid tableId");
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (!table->rowBloomFilter) {
		err = initRowBloomFilterByTable(tableId); if (err) { 
			printm("row bloom filter init fail\n");
			return TRUE; }
	}
	ASSERT(!table->rowBloomFilter, "row bloom filter not inited\n");
	uint8* ptr = table->rowBloomFilter->buff;
	//uint64 start = myCounterStart();
	uint32 maxLen = table->rowBloomFilter->size / sizeof(uint8);
	uint32 index1 = HashMurmur3_32(&rowId, sizeof(rowId), 0) % maxLen;
	uint32 index2 = HashXXHash_32(&rowId, sizeof(rowId), 0) % maxLen;
	uint32 index3 = HashFnvHash(&rowId, sizeof(rowId)) % maxLen;
	uint32 index4 = HashDjb2Hash(&rowId, sizeof(rowId)) % maxLen;

	if (ptr[index1] && ptr[index3] && ptr[index2] && ptr[index4]) {
		isLock = TRUE;
	}

	//uint64 us = myCounterUs(start);
	//printm("check rowBloomFilter %lluus\n", us);
	return isLock;
}

static int loadTableInfo(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (!StorageIsFileExist(GET_TABLE_INFO_FILE_ID(tableId))) { return -1; }
	uint64 size = StorageGetFileSize(GET_TABLE_INFO_FILE_ID(tableId));
	if (size < sizeof(TableInfo)) {
		ASSERT(TRUE, "invalid table info file\n");
		return -2;
	}

	uint64 offset = 0;
	err = StorageRead(GET_TABLE_INFO_FILE_ID(tableId), 0, size, &(table->infoCache), &offset);
	if (err) {
		ASSERT(TRUE, "load table info fail\n");
		return -3;
	}
	table->info = table->infoCache->buff + offset;
	for (uint32 i = 0; i < table->info->colNum;i++) {
		table->rowSize += sqlCmdGetColSize(table->info->colInfo + i);
	}
	
	err = initRVT(tableId);
	if (err) {
		ASSERT(TRUE, "init RVT fail");
		return -4;
	}
	table->flag.isValid = TRUE;
	return 0;
}
static void printDividLine(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	char buff[1024] = { 0 };
	uint32 cnt = 0;
	uint32* maxLen = table->columnMaxStrLen->buff;
	for (uint32 i = 0; i < table->info->colNum;i++) {
		cnt += maxLen[i];
	}
	cnt += table->info->colNum + 1;
	memset(buff, '-', cnt); buff[strlen(buff)] = '\n';
	printm(buff);
}
static void printDividLineByColList(uint64 tableId, uint64* colId, uint32 cnt) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	char buff[1024] = { 0 };
	char* ptr = buff;
	uint32* maxValueLen = table->columnMaxStrLen->buff;
	for (uint32 i = 0; i < table->info->colNum; i++) {
		bool isInList = FALSE;
		for (uint32 j = 0; j < cnt; j++) {
			if (colId[j] == i) {
				isInList = TRUE;
				break;
			}
		}
		if (!isInList) { continue; }
		memset(ptr, '-', maxValueLen[i]); ptr += (maxValueLen[i]);
	}
	memset(ptr, '-', cnt + 1); ptr += (cnt+1);
	ptr[0] = '\n';
	printm(buff);
}
static void viewTableNameList(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	char buff[1024] = { 0 };
	printDividLine(tableId);
	char* ptr = buff;
	ptr[0] = '|'; ptr++;
	uint32* maxValueLen = table->columnMaxStrLen->buff;
	for (uint32 i = 0; i < table->info->colNum; i++) {
		uint32 curLen = MIN(strlen(table->info->colInfo[i].name), COLUMN_NAME_LEN_MAX);
		memcpy(ptr, table->info->colInfo[i].name, curLen); ptr += curLen;
		memset(ptr, ' ', maxValueLen[i] - curLen); ptr += (maxValueLen[i] - curLen);
		ptr = buff + (strlen(buff));
		ptr[0] = '|'; ptr++;
	}
	ptr[0] = '\n';
	printm(buff);
	printDividLine(tableId);
}
static void viewTableNameListByColIdList(uint64 tableId, uint64* colId, uint32 cnt) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	char buff[1024] = { 0 };
	printDividLineByColList(tableId, colId, cnt);
	char* ptr = buff;
	ptr[0] = '|'; ptr++;
	uint32* maxValueLen = table->columnMaxStrLen->buff;
	for (uint32 i = 0; i < table->info->colNum; i++) {
		bool isInList = FALSE;
		for (uint32 j = 0; j < cnt; j++) {
			if (colId[j] == i) {
				isInList = TRUE;
				break;
			}
		}
		if (!isInList) { continue; }
		uint32 curLen = MIN(strlen(table->info->colInfo[i].name), COLUMN_NAME_LEN_MAX);
		memcpy(ptr, table->info->colInfo[i].name, curLen); ptr += curLen;
		memset(ptr, ' ', maxValueLen[i] - curLen); ptr += (maxValueLen[i] - curLen);
		ptr = buff + (strlen(buff));
		ptr[0] = '|'; ptr++;
	}
	ptr[0] = '\n';
	printm(buff);
	printDividLineByColList(tableId, colId, cnt);
}
static void viewTableInfo(uint64 tableId) {
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	printm("****** table [%llu] %s ******\n", table->info->id, table->info->name);
	for (uint32 i = 0; i < table->info->colNum;i++) {
		printm("[");
		sqlCmdViewColumnInfo(table->info->colInfo + i);
		//printm("[%s %s(%llu)]", table->info->colInfo[i].name, gl_dataTypeStr[table->info->colInfo[i].dataType], table->info->colInfo[i].size);
		printm("]");
	}
	printm("\n");

}
static void viewRVT(uint64 tableId) {
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	for (uint64 i = 0;i < table->rvt->size*8;i++) {
		if (checkBitArrayByIndex(table->rvt->buff, i)) {
			printm("[%llu]", i);
		}
	}
	printm("\n");
}
static void viewTotalTable(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 cnt = table->rvt->size * 8;
	uint32 validRowCnt = 0;
	viewTableInfo(tableId);
	viewTableNameList(tableId);
	for (uint64 i = 0; i < cnt;i++) {
		if (checkBitArrayByIndex(table->rvt->buff, i)) {
			BufferListInfo* load = NULL;
			uint64 offset = 0;
			err = ChangeLogTryReadFromLog(GET_TABLE_FILE_ID(tableId), 
				i*(sizeof(RowTag) + table->rowSize), 
				sizeof(RowTag) + table->rowSize, 
				&load, &offset);
			if (err == 1) {
				ChangeLogUpdateBackgroundLogToStorage();
				ChangeLogCheckpointBegin();
				ChangeLogCheckpointDone();
				goto ReadFromTable;
			}
			else if (err == 2) {
				ReadFromTable:
				ASSERT(load, "not found in log but dataBuffer is not null");
				err = StorageRead(GET_TABLE_FILE_ID(tableId),
					i * (sizeof(RowTag) + table->rowSize),
					sizeof(RowTag) + table->rowSize,
					&load, &offset);
				if (err) {
					ASSERT(TRUE, "row is valid but can not read");
					return;
				}
			}
			RowTag* tag = load->buff + offset;
			char* data = tag+1;
			uint32 rowSeek = 0;
			viewRow(tableId, data);
			validRowCnt++;
			BuffRelease(&load);
		}
	}
	printDividLine(tableId);
	printm("***** %llu row ********\n", validRowCnt);
}
static void viewColumn(uint64 tableId, uint64 columnId, char* value) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);

	char buff[1024] = { 0 };
	char* ptr = buff;
	if (table->info->colInfo[columnId].dataType == SQL_DATA_STRING || 
		table->info->colInfo[columnId].dataType == SQL_DATA_BINARY) {
		//printm("%.*s", table->info->colInfo[columnId].strMaxLen, value);
		sprintf(buff, "%.*s", table->info->colInfo[columnId].strMaxLen, value);
	}
	else if (table->info->colInfo[columnId].dataType == SQL_DATA_INT8) {
		int8* tmp = value;
		sprintf(buff, "%d", *tmp);
	}
	else if (table->info->colInfo[columnId].dataType == SQL_DATA_INT16) {
		int16* tmp = value;
		sprintf(buff, "%d", *tmp);
	}
	else if (table->info->colInfo[columnId].dataType == SQL_DATA_INT32) {
		int32* tmp = value;
		sprintf(buff, "%d", *tmp);
	}
	else if (table->info->colInfo[columnId].dataType == SQL_DATA_INT64) {
		int64* tmp = value;
		sprintf(buff, "%lld", *tmp);
	}
	else if (table->info->colInfo[columnId].dataType == SQL_DATA_FLOAT) {
		float* tmp = value;
		sprintf(buff, "%g", *tmp);
	}
	else if (table->info->colInfo[columnId].dataType == SQL_DATA_DOUBLE) {
		double* tmp = value;
		sprintf(buff, "%g", *tmp);
	}
	else {
		printm("[unkown type]");
		ASSERT(TRUE,"unkown data type");
		return;
	}
	ptr = buff + strlen(buff);
	uint32* maxLen = table->columnMaxStrLen->buff;;
	memset(ptr, ' ', maxLen[columnId] - strlen(buff));
	printm(buff);
}
static void viewRow(uint64 tableId, char* data) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 rowSeek = 0;

	printm("|");
	for (uint32 i = 0; i < table->info->colNum; rowSeek += sqlCmdGetColSize(table->info->colInfo + i), i++) {
		viewColumn(tableId, i, data + rowSeek);
		printm("|");
	}
	printm("\n");
}
static void viewSelectOutput(SqlTokenHandle tk, BufferListInfo* output) {
	int err = 0;
	uint64 tableId = tk.tableId;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint32 colCnt = tk.destColList->buff[0] == '*' ? table->info->colNum : tk.destColList->size / sizeof(uint64);
	uint64 colIdListTotalSize = tk.destColList->buff[0] == '*' ? table->rowSize : getColumnIdListTotalValueSize(tableId, tk.destColList->buff, colCnt);
	uint64 rowCnt = output->size / colIdListTotalSize;
	char* rowPtr = output->buff;
	if (tk.destColList->buff[0] == '*') {
		viewTableNameList(tableId);
		for (uint64 i = 0; i < rowCnt;i++, rowPtr += colIdListTotalSize) {
			viewRow(tableId, rowPtr);
		}
		printDividLine(tableId);
	}
	else {
		uint64* colId = tk.destColList->buff;
		viewTableNameListByColIdList(tableId, colId, colCnt);
		for (uint32 i = 0; i < rowCnt; i++) {
			colId = tk.destColList->buff;
			printm("|");
			for (uint32 j = 0; j < colCnt; j++, colId++) {
				viewColumn(tableId, *colId, rowPtr);
				rowPtr += sqlCmdGetColSize(table->info->colInfo + *colId);
				printm("|");
			}
			printm("\n");
		}
		colId = tk.destColList->buff;
		printDividLineByColList(tableId, colId, colCnt);
	}
	
	
}
static void viewTableList(uint64* tableId, uint32 cnt) {
	char buff[TABLE_NAME_LEN_MAX + 5] = { 0 };
	uint32 maxNameLen = 0;
	uint32 showCnt = 0;
	if (tableId) {
		for (uint32 i = 0; i < cnt; i++) {
			uint32 curNameLen = strlen(gl_tableList[tableId[i]].info->name);
			if (maxNameLen < curNameLen) {
				maxNameLen = curNameLen;
			}
		}
		maxNameLen = ROUND_UP(maxNameLen, 8);
		if (maxNameLen > 0) {
			for (uint32 i = 0; i < cnt; i++) {
				memset(buff, '-', maxNameLen+1); printm(buff); printm("\n"); memset(buff, 0, sizeof(buff));
				uint32 curNameLen = strlen(gl_tableList[tableId[i]].info->name);
				char* ptr = buff;
				*ptr = '|';	ptr++;
				memcpy(ptr, gl_tableList[tableId[i]].info->name, curNameLen);	ptr += curNameLen;
				uint32 tabCnt = CEIL_DIVIDE((maxNameLen - (ptr - buff)), 8);
				memset(ptr, '\t', tabCnt);
				printm(buff); printm("|\n"); memset(buff, 0, sizeof(buff));
				showCnt++;
			}
			if(showCnt)
				memset(buff, '-', maxNameLen+1); printm(buff); printm("\n"); memset(buff, 0, sizeof(buff));
		}
	}
	else {
		// *
		for (uint32 i = 0; i < TABLE_CNT_MAX; i++) {
			if (gl_tableList[i].info) {
				uint32 curNameLen = strlen(gl_tableList[i].info->name);
				if (maxNameLen < curNameLen) {
					maxNameLen = curNameLen;
				}
			}
		}
		maxNameLen = ROUND_UP(maxNameLen, 8);
		if (maxNameLen > 0) {
			
			for (uint32 i = 0; i < TABLE_CNT_MAX; i++) {
				if (gl_tableList[i].info) {
					memset(buff, '-', maxNameLen+1); printm(buff); printm("\n"); memset(buff, 0, sizeof(buff));
					uint32 curNameLen = strlen(gl_tableList[i].info->name);
					char* ptr = buff;
					*ptr = '|';	ptr++;
					memcpy(ptr, gl_tableList[i].info->name, curNameLen);	ptr += curNameLen;
					uint32 tabCnt = CEIL_DIVIDE((maxNameLen - (ptr - buff)),8);
					memset(ptr, '\t', tabCnt);
					printm(buff); printm("|\n"); memset(buff, 0, sizeof(buff));
					showCnt++;
				}
			}
			if(showCnt)	
				memset(buff, '-', maxNameLen+1); printm(buff); printm("\n"); memset(buff, 0, sizeof(buff));
		}
	}

	if (showCnt == 0) {
		printm("no any tables!\n");
	}

}

static int init() {
	//need Change Log inited
	//need Storage engine inited
	uint64 tableCnt = 0;
	int err = 0;
	static bool inited = FALSE;
	if (inited) { return 0; }
	printm("current table:");
	for (uint64 tableId = 0; tableId < TABLE_CNT_MAX; tableId++) {
		err = loadTableInfo(tableId);
		if (err == 0) { 
			printm("[%llu]", tableId); 
			tableCnt++; 

			err = initMaxStrLen_ScanTable(tableId); if (err) { ASSERT(TRUE, "init column max string len fail\n"); return -1; }
#ifdef DEBUG_TABLE_MANAGER
			TableHandle* table = GET_TABLE_HANDLE(tableId);
			err = SqlDecodeSetTableInfo(tableId, table->info, sizeof(TableInfo) + table->info->colNum * sizeof(SqlColumnItem));
			ASSERT(err, "set decode module table info fail");
#endif // DEBUG_TABLE_MANAGER
			err = initRowBloomFilterByTable(tableId); if (err) { ASSERT(TRUE, "init row bloomFilter fail\n"); return -1; }
		}
		else if(err == -1){
			//not exist
		}
		else { return -1; }
	}
	printm("\n");
	printm("***total %llu tables***\n", tableCnt);
	inited = TRUE;
	return 0;
}
static int createTable(uint64 tableId, char name[TABLE_NAME_LEN_MAX], uint32 columnCnt, char* colInfoList) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (table->infoCache) {
		return -1;
	}

	if (StorageIsFileExist(GET_TABLE_INFO_FILE_ID(tableId))) { return -1; }
	table->infoCache = getBuffer(sizeof(TableInfo) + (columnCnt)*sizeof(SqlColumnItem), BUFFER_POOL);
	if (!table->infoCache) {
		ASSERT(TRUE, "create table info but buffer not enough");
		return -2;
	}
	TableInfo* tInfo = table->infoCache->buff;
	tInfo->id = tableId;
	memcpy(tInfo->name, name, strlen(name));
	tInfo->colNum = columnCnt;
	for (uint32 i = 0; i < columnCnt;i++) {
		memcpy(&tInfo->colInfo[i], colInfoList + i * (sizeof(SqlColumnItem)), sizeof(SqlColumnItem));
	}

	BufferListInfo* seekList = getBuffer(sizeof(uint64), BUFFER_POOL); if (!seekList) { return -2; }
	uint64* seek = seekList->buff; *seek = 0;
	BufferListInfo* dataList = table->infoCache;
	BufferListInfo* sizeList = getBuffer(sizeof(uint64), BUFFER_POOL); if (!sizeList) { return -2; }
	uint64* size = sizeList->buff; *size = table->infoCache->size;

	err = StoragePushCommand(GET_TABLE_INFO_FILE_ID(tableId), 1, seekList, dataList, sizeList);
	if (err) {
		BuffRelease(&seekList);
		BuffRelease(&sizeList);
		BuffRelease(&dataList);
		return -3; 
	}
	StorageDoCommand();	
	
	return 0;
}
static int deleteTable(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (table->infoCache == NULL) {
		return -1;
	}
	if (ChangeLogIsDestIdInLog(GET_TABLE_FILE_ID(tableId)) || 
		ChangeLogIsDestIdInLog(GET_TABLE_INFO_FILE_ID(tableId))) {
		return 1;
	}
	
	err = StorageRemoveFile(GET_TABLE_INFO_FILE_ID(tableId));	if (err) { ERROR(-2); }
	if (StorageIsFileExist(GET_TABLE_FILE_ID(tableId))) {
		err = StorageRemoveFile(GET_TABLE_FILE_ID(tableId));	if (err) { ERROR(-3); }
	}
	
	BuffRelease(&table->infoCache);
	BuffRelease(&table->rvt);
	memset(table, 0, sizeof(TableHandle));
	
	return 0;
error:
	ASSERT(TRUE, "delete table fail[%d]", err);
	return err;
}
static void cleanTable(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);

	freeBuffer(&table->infoCache);
	freeBuffer(&table->columnMaxStrLen);
	freeBuffer(&table->rvt);
	freeBuffer(&table->rclock);
	freeBuffer(&table->rowBloomFilter);
	memset(table, 0, sizeof(TableHandle));
	return;
}
static int setRVT(uint64 tableId, uint64 rowId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (table->rvt->size * 8 <= rowId) {
		err = BuffRealloc(&table->rvt, ROUND_UP((rowId / 8) + 1, TABLE_RVT_BLOCK_SIZE), BUFFER_POOL);
		if (err) {
			ASSERT(TRUE, "rvt buffer not enough");
			return -2;
		}
	}
	setBitArrayByIndex(table->rvt->buff, rowId);
	return 0;
}
static void cleanRVT(uint64 tableId, uint64 rowId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (table->rvt->size * 8 < rowId + 1) {
		ASSERT(TRUE, "rowId out of rvt range");
		return;
	}
	cleanBitArrayByIndex(table->rvt->buff, rowId);
	if (rowId < table->freeRowId) {
		table->freeRowId = rowId;
	}
}
static bool checkRVT(uint64 tableId, uint64 rowId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (!table->rvt) {
		return FALSE;
	}
	if (table->rvt->size * 8 <= rowId) {
		return FALSE;
	}
	return checkBitArrayByIndex(table->rvt->buff, rowId);
}

static int setRCLock(uint64 tableId, uint64 rowId, uint64 colId) {
	//rowLock*1 + colLock * n
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 index = rowId * (table->info->colNum + 1) + 1 + colId;
	if (!table->rclock) {
		err = extendRCLock(tableId, index); if (err) { return -2; }
	}
	if (table->rclock->size * 8 <= index) {
		err = extendRCLock(tableId, index); if (err) { return -2; }
	}
	if (checkBitArrayByIndex(table->rclock->buff, index)) {
		return -1;
	}
	setBitArrayByIndex(table->rclock->buff, index);
	return 0;
}
static int setRowLock(uint64 tableId, uint64 rowId) {
	//rowLock*1 + colLock * n
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 index = rowId * (table->info->colNum + 1);
	if (!table->rclock) {
		err = extendRCLock(tableId, index); if (err) { return -2; }
	}
	if (table->rclock->size * 8 <= index) {
		err = extendRCLock(tableId, index); if (err) { return -2; }
	}
	if (checkBitArrayByIndex(table->rclock->buff, index)) {
		return -1;
	}
	setBitArrayByIndex(table->rclock->buff, index);
	return 0;
}
static bool checkRowLock(uint64 tableId, uint64 rowId) {
	//rowLock*1 + colLock * n
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 index = rowId * (table->info->colNum + 1);
	if (!table->rclock) {
		return FALSE;
	}
	if (table->rclock->size * 8 <= index) {
		return FALSE;
	}
	return checkBitArrayByIndex(table->rclock->buff, index);
}
static void cleanRowLock(uint64 tableId, uint64 rowId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 index = rowId * (table->info->colNum + 1);
	if (table->rclock->size * 8 <= index) {
		ASSERT(table->rclock->size * 8 <= index, "cleaning not valid row lock");
		return;
	}
	cleanBitArrayByIndex(table->rclock->buff, index);
	return;
}
static void cleanRCLock(uint64 tableId, uint64 rowId, uint64 colId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 index = rowId * (table->info->colNum + 1) + 1 + colId;
	if (table->rclock->size * 8 <= index) {
		ASSERT(table->rclock->size * 8 <= index, "cleaning not valid RC lock");
		return;
	}
	cleanBitArrayByIndex(table->rclock->buff, index);
	return;
}
static bool checkRCLock(uint64 tableId, uint64 rowId, uint64 colId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 index = rowId * (table->info->colNum + 1) + 1 + colId;
	if (!table->rclock) {
		return FALSE;
	}
	if (table->rclock->size * 8 <= index) {
		return FALSE;
	}
	checkBitArrayByIndex(table->rclock->buff, index);
	return;
}
static uint64 getColIdByName(uint64 tableId, char* columnName, uint32 len) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	for (uint32 i = 0; i < table->info->colNum; i++) {
		if (strncmp(table->info->colInfo[i].name, columnName, len) == 0) {
			return i;
		}
	}
	return INVALID_COLUMN_ID;
}
static int strColumnListToColumnIdList(uint64 tableId, BufferListInfo** strColList) {
	int err = 0;
	char* ptr = (*strColList)->buff;
	uint32 len = (*strColList)->size;
	BufferListInfo* colIdList = NULL;
	if (*ptr == '*' && len == 1) {
		return 0;
	}
	else {
		char* next = strchr(ptr + 1, ',');
		uint32 cnt = strCountChar(ptr, len, ',') + 1;
		colIdList = getBuffer(cnt * sizeof(uint64), BUFFER_POOL); if (!colIdList) { return -2; }
		uint64* colId = colIdList->buff;

		for (uint32 i = 0; i < cnt; i++, colId++) {
			*colId = getColIdByName(tableId, ptr, next?next-ptr:((*strColList)->size - (ptr - (*strColList)->buff)));
			if (*colId == INVALID_COLUMN_ID) {
				ASSERT(*colId == INVALID_COLUMN_ID, "getColIdByName fail");
				ERROR(-3);
			}
			if (next) {
				ptr = next + 1;
				next = strchr(ptr, ',');
			}
		}
	}

	
	BuffRelease(strColList);
	*strColList = colIdList;
	
	return 0;

error:
	BuffRelease(&colIdList);
	return err;
}
static int strTableNameListToTableIdList(BufferListInfo** strTableList) {
	if (*strTableList) {
		uint32 cnt = strCountChar((*strTableList)->buff, (*strTableList)->size, ',') + 1;
		BufferListInfo* tableIdList = getBuffer(cnt * sizeof(uint64), BUFFER_POOL); if (!tableIdList) { return -2; }
		uint64* id = tableIdList->buff;
		char* ptr = (*strTableList)->buff;
		char* next = strchr(ptr, ',');
		for (uint32 i = 0; i < cnt; i++,id++) {
			*id = getTableIdByName(ptr, next?next-ptr:((*strTableList)->size - (ptr- (*strTableList)->buff)));

			if (next) {
				ptr = next + 1;
				next = strchr(ptr, ',');
			}
		}

		BuffRelease(strTableList);
		*strTableList = tableIdList;
	}
	
	return 0;
}
static uint64 getTableIdByName(char* name, uint32 len) {
	uint64 id = 0;
	for (id = 0; id < TABLE_CNT_MAX; id++) {
		if (gl_tableList[id].flag.isValid) {
			if (strncmp(gl_tableList[id].info->name, name, len) == 0) {
				return id;
			}
		}
	}

	return INVALID_TABLE_ID;
}
static int strTableToId(BufferListInfo** strTable) {
	int err = 0;
	char* destTableName = (*strTable)->buff;
	TableHandle* table = gl_tableList;
	for (uint32 i = 0; i < TABLE_CNT_MAX;i++, table++) {
		if (table->info) {
			if (strncmp(table->info->name, destTableName, (*strTable)->size) == 0) {
				BufferListInfo* tableId = getBuffer(sizeof(uint64), BUFFER_POOL);
				uint64* id = tableId->buff;
				*id = table->info->id;
				BuffRelease(strTable);
				(*strTable) = tableId;
				return 0;
			}
		}
	}
	return -1;
}
static valueType getValueTypeByDataType(sqlDataType dataType) {
	switch (dataType) {
		case SQL_DATA_INT8	 :
		case SQL_DATA_INT16	 :
		case SQL_DATA_INT32	 :
		case SQL_DATA_INT64	 :
		case SQL_DATA_TABLE	 :
		case SQL_DATA_FLOAT	 :
		case SQL_DATA_DOUBLE :
			return VT_num;
		case SQL_DATA_STRING :
		case SQL_DATA_IMAGE	 :
		case SQL_DATA_BINARY :
			return VT_binary;
		case SQL_DATA_MAX	 :
		case SQL_DATA_INVALID:
		default:
			ASSERT(TRUE, "invalid dataType");
			return VT_binary;
	}
}
static int getColumnValueByNameCB(uint64 tableId, char* row, char* columnName, uint32 len, BufferListInfo** outValue) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	char* ptr = row;
	for (uint32 i = 0; i < table->info->colNum;i++) {
		int curColSize = sqlCmdGetColSize(table->info->colInfo + i);
		ASSERT(curColSize <= 0, "invalid col size\n");
		if (strncmp(table->info->colInfo[i].name, columnName, len) == 0) {
			(*outValue) = getBuffer(curColSize + sizeof(valueInfo), BUFFER_POOL);
			valueInfo* info = (*outValue)->buff;
			memcpy(info+1, ptr, curColSize);
			info->dataType = table->info->colInfo[i].dataType;
			info->valueType = getValueTypeByDataType(table->info->colInfo[i].dataType);
			return 0;
		}
		ptr += curColSize;
	}
	return -1;
}
static char* getColValuePtrByName(uint64 tableId, char* row, char* columnName, uint32 len) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	char* ptr = row;
	for (uint32 i = 0; i < table->info->colNum; i++) {
		if (strncmp(table->info->colInfo[i].name, columnName, len) == 0) {
			return ptr;
		}
		ptr += sqlCmdGetColSize(table->info->colInfo + i);
	}
	return NULL;
}
static char* getColValuePtrById(uint64 tableId, char* row, uint64 colId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	char* ptr = row;
	for (uint32 i = 0; i < table->info->colNum;i++) {
		if (i == colId) {
			return ptr;
		}
		ptr += sqlCmdGetColSize(table->info->colInfo + i);
	}
	return NULL;
}
static uint64 getColumnIdListTotalValueSize(uint64 tableId, uint64* colIdList, uint32 cnt) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 size = 0;
	for (uint32 i = 0; i < cnt;i++) {
		if (colIdList[i] >= table->info->colNum) {
			ASSERT(colIdList[i] >= table->info->colNum, "invalid column id");
			return 0;
		}
		size += sqlCmdGetColSize(table->info->colInfo + colIdList[i]);
	}
	return size;
}
static uint64 getMaxValidRowIdByRVT(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (!table->rvt) { return U64_MAX; }
	uint64 ret = getBitArrayMaxIndex(table->rvt->buff, table->rvt->size * 8);
	if (ret == U64_MAX) {
		if (table->rvt->buff[0] > 0) {
			ret = table->rvt->size * 8;
		}
		else {
			ret = U64_MAX;
		}
	}
	if (ret != U64_MAX) {
		ASSERT(!checkRVT(tableId, ret), "not valid row\n");
	}
	return ret;
}
static int initMaxStrLen_ScanTable(uint64 tableId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	BuffRelease(&table->columnMaxStrLen);
	table->columnMaxStrLen = getBuffer(table->info->colNum * sizeof(uint32), BUFFER_POOL); if (!table->columnMaxStrLen) { return -2; }
	
	uint32* colMaxLen = table->columnMaxStrLen->buff;
	uint64 remainRowNum = table->rowNum;
	for (uint64 rowId = 0; remainRowNum > 0;rowId++) {
		if (checkRVT(tableId, rowId)) {
			BufferListInfo* load = NULL;
			uint64 offset = 0;
			err = selectRow(tableId, rowId, &load, &offset); if (err) { return -1; }
			RowTag* tag = load->buff + offset;
			char* row = tag + 1;
			
			for (uint64 colId = 0; colId < table->info->colNum; colId++) {
				char* value = getColValuePtrById(tableId, row, colId);
				uint32 strLen = sqlCmdGetColumnValueStrLen(value, sqlCmdGetColSize(table->info->colInfo + colId), table->info->colInfo[colId].dataType);
				colMaxLen[colId] = MAX(colMaxLen[colId], strLen);
				colMaxLen[colId] = MAX(colMaxLen[colId], strlen(table->info->colInfo[colId].name));
			}


			remainRowNum--;
			BuffRelease(&load);
		}
	}

	return 0;
}

static int createRow(uint64 tableId, char* dataList, uint64* sizeList) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	TableInfo* tInfo = table->info;
	BufferListInfo* rowData = getBuffer(table->rowSize + sizeof(RowTag), BUFFER_POOL);
	RowTag* tag = rowData->buff;
	char* col = tag + 1;
	uint64 fileSeek = table->freeRowId*(rowData->size);
	tag->isValid = 1;
	char* data = dataList;
	for (uint32 i = 0; i < tInfo->colNum;i++) {
		memcpy(col, data, sizeList[i]);
		int colSize = sqlCmdGetColSize(table->info->colInfo + i);
		col += colSize;
		data += colSize;
	}
	err = ChangeLogInsert(GET_TABLE_FILE_ID(tableId), LOG_TYPE_WRITE, fileSeek, rowData, 0, rowData->size, NULL, NULL);
	BuffRelease(&rowData);
	if (err >= 0) {
		setRVT(tableId, table->freeRowId);
		table->freeRowId = findNextFreeRow(tableId, table->freeRowId);
	}

	return err;
error:
	ASSERT(TRUE, "create row fail[%d]", err);
	BuffRelease(&rowData);
	return -2;
}
static int insertRow(uint64 tableId, SqlInsertColumnInfo* colList, uint64 len) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	
	BufferListInfo* rowData = getBuffer(sizeof(RowTag) + table->rowSize, BUFFER_POOL); if (!rowData) { return -1; }
	RowTag* tag = rowData->buff;
	char* row = tag + 1;

	SqlInsertColumnInfo* info = colList;
	char* value = info + 1;
	for (uint32 i = 0; i < table->info->colNum;i++) {
		uint64 colId = getColIdByName(tableId, info->name, strlen(info->name));
		if (colId == INVALID_COLUMN_ID) {
			ERROR(-1);
		}
		if (table->info->colInfo[colId].strMaxLen < info->dataLen) {
			ERROR(-1);
		}

		char* colValuePtr = getColValuePtrById(tableId, row, colId);
		memcpy(colValuePtr, value, info->dataLen);

		uint32 curValueStrLen = sqlCmdGetColumnValueStrLen(value, table->info->colInfo[colId].strMaxLen, info->type);
		uint32* maxLenList = table->columnMaxStrLen->buff;
		maxLenList[i] = MAX(maxLenList[i], curValueStrLen);

		info = value + info->dataLen;
		value = info + 1;
	}

	uint64 fileSeek = table->freeRowId * (rowData->size);
	tag->isValid = 1;
	err = ChangeLogInsert(GET_TABLE_FILE_ID(tableId), LOG_TYPE_WRITE, fileSeek, rowData, 0, rowData->size, NULL, NULL);
	BuffRelease(&rowData);
	if (err >= 0) {
		setRVT(tableId, table->freeRowId);
		table->freeRowId = findNextFreeRow(tableId, table->freeRowId);
		table->rowNum++;
	}

	return err;
error:
	BuffRelease(&rowData);
	return -3;
}
static int deleteRow(uint64 tableId, uint64 rowId) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (!checkRVT(tableId, rowId)) {
		return -1;
	}
	uint64 totalRowSize = table->rowSize + sizeof(RowTag);
	uint64 seek = rowId * totalRowSize;
	BufferListInfo* data = getBuffer(totalRowSize, BUFFER_POOL);
	if (!data) {
		ASSERT(TRUE, "buffer not enough when deleteRow");
		return -2;
	}
	err = ChangeLogInsert(GET_TABLE_FILE_ID(tableId), LOG_TYPE_WRITE, seek, data, 0, totalRowSize, NULL, NULL);
	BuffRelease(&data);
	if (err >= 0) {
		cleanRVT(tableId, rowId);
		table->rowNum--;
	}

	return err;
}
static int updateColumn(uint64 tableId, uint64 rowId, uint64 colId, char* data, uint64 size) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	int colSize = sqlCmdGetColSize(table->info->colInfo + colId);
	if (table->info->colNum < colId + 1 || size > colSize || colSize < 0) {
		return -1;
	}
	BufferListInfo* newColData = getBuffer(colSize, BUFFER_POOL); if (!newColData) { return -2; }
	memcpy(newColData->buff, data, size);
	uint64 seek = rowId * (sizeof(RowTag) + table->rowSize) + sizeof(RowTag);
	for (uint32 i = 0; i < colId;i++) {
		seek += sqlCmdGetColSize(table->info->colInfo + i);
	}

	err = ChangeLogInsert(GET_TABLE_FILE_ID(tableId), LOG_TYPE_WRITE, seek, newColData, 0, newColData->size, NULL, NULL);

	return err;
}
static int updateColumnToRowBuffer(uint64 tableId, char* row, uint64 colId, char* data, uint64 size) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	int colSize = sqlCmdGetColSize(table->info->colInfo + colId);
	if (table->info->colNum < colId + 1 || size > colSize || colSize < 0) {
		return -1;
	}
	char* colPtr = getColValuePtrById(tableId, row, colId);
	memset(colPtr, 0, colSize);
	memcpy(colPtr, data, size);
	return 0;
}
static int getDestRowListForUpdate(uint64 tableId, SqlTokenHandle tk, BufferListInfo** rowList) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 remainRowCnt = table->rowNum;
	BufferListInfo* load = NULL;
	BufferListInfo* ret = NULL;
	uint32 matchCnt = 0;
	ASSERT(!tk.whereStack, "where exp null\n");
	for (uint64 i = 0;remainRowCnt>0;i++) {
		if (!checkRVT(tableId, i)) {
			continue;
		}
		
		uint64 offset = 0;
		err = selectRow(tableId, i, &load, &offset); if (err < 0 || err == 2 || !load) { ERROR(-3); }
		if (err == 1) {
			return 2;
		}
		RowTag* tag = load->buff;
		char* row = tag + 1;
		bool isMatch = FALSE;
		if (tk.whereStack) {
			isMatch = SqlHandleCalculateExpression(tableId, row, &tk.whereStack, getColumnValueByNameCB);
		}
		if (isMatch) {
			if (ret) {
				err = BuffRealloc(&ret, ret->size + sizeof(uint64), BUFFER_POOL); if (err) { ERROR(-2); }
			}
			else {
				ret = getBuffer(sizeof(uint64), BUFFER_POOL); if (!ret) { ERROR(-2); }
			}
			uint64* id = ret->buff; id += matchCnt;
			*id = i;
			matchCnt++;
		}

		BuffRelease(&load);
		remainRowCnt--;
	}
	*rowList = ret;
	return matchCnt == 0?1:0;
error:

	BuffRelease(&load);
	BuffRelease(&ret);
	return -3;
}
static int getDestUpdateColIdAndValue(BufferListInfo** setExp, uint64* colId) {
	uint32 cnt = (*setExp)->size / sizeof(BufferListInfo*);
	return SqlHandleCalculateMultipleExpression(setExp, cnt);

}
static int selectRow(uint64 tableId, uint64 rowId, BufferListInfo** data, uint64* offset) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	if (!checkRVT(tableId, rowId)) {
		return -1;//not valid row
	}

	uint64 seek = rowId * (table->rowSize + sizeof(RowTag));
	err = ChangeLogTryReadFromLog(GET_TABLE_FILE_ID(tableId), seek, table->rowSize + sizeof(RowTag), data, offset);
	if (err == 1) {
		return 1;
	}
	else if (err == 2) {
		//pass
	}
	else if (err == -2) {
		return 2;
	}
	else if (err == 0) {
		return 0;
	}
	else {
		ASSERT(TRUE,"try read from change log fail when select row");
	}

	//read table
	err = StorageRead(GET_TABLE_FILE_ID(tableId), seek, table->rowSize + sizeof(RowTag), data, offset); if (err) { return -2; }

	return 0;
}
static int selectRowList_ScanTable(uint64 tableId, SqlTokenHandle tk, BufferListInfo** out) {
	//tk has changed to id list from str list
 	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 remainRowCnt = table->rowNum;
	BufferListInfo* load = NULL;
	BufferListInfo* ret = NULL;
	uint32 matchCnt = 0;
	uint32 destColCnt = tk.destColList->buff[0] == '*' ? table->info->colNum : tk.destColList->size / sizeof(uint64);
	uint64 totalSize = tk.destColList->buff[0] == '*' ? table->rowSize : getColumnIdListTotalValueSize(tableId, tk.destColList->buff, destColCnt);

	for (uint64 i = 0; remainRowCnt>0;i++) {
		uint64 seek = GET_SEEK_BY_ROW_ID(i, table->rowSize);
		uint64 offset = 0;
		if (!checkRVT(tableId, i)) { continue; }
		err = selectRow(tableId, i, &load, &offset);
		if (err == 1) {
			return 1;
		}
		ASSERT(err,"select error");
		if (err == 0) {
			RowTag* tag = load->buff + offset;
			char* row = tag + 1;
			bool isMatch = TRUE;
			if (tk.whereStack) {
				isMatch = SqlHandleCalculateExpression(tableId, row, &tk.whereStack, getColumnValueByNameCB);
			}
			if (isMatch) {
				if (ret) {
					err = BuffRealloc(&ret, ret->size + totalSize, BUFFER_POOL); if (err) { ERROR(-3); }
				}
				else {
					ret = getBuffer(totalSize, BUFFER_POOL); if (!ret) { ERROR(-3); }
				}

				char* outPtr = ret->buff + matchCnt * totalSize;
				if (tk.destColList->buff[0] == '*') {
					memcpy(outPtr, row, totalSize);
				}
				else {
					uint64* destColId = tk.destColList->buff;
					for (uint32 i = 0; i < destColCnt; i++, destColId++) {
						char* colValue = getColValuePtrById(tableId, row, *destColId);
						memcpy(outPtr, colValue, sqlCmdGetColSize(table->info->colInfo + *destColId));
						outPtr += (sqlCmdGetColSize(table->info->colInfo + *destColId));
					}
				}
				matchCnt++;
			}

			remainRowCnt--;
		}
		if(load)
			BuffRelease(&load);
	}

	*out = ret;
	return 0;
error:
	BuffRelease(&load);
	BuffRelease(&ret);
	
	return err;
}
static int deleteRow_ScanTable(uint64 tableId, SqlTokenHandle tk) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);
	uint64 remainRowCnt = table->rowNum;
	BufferListInfo* load = NULL;

	uint32 matchCnt = 0;
	for (uint64 i = 0; remainRowCnt > 0; i++) {
		uint64 seek = GET_SEEK_BY_ROW_ID(i, table->rowSize);
		uint64 offset = 0;
		if (!checkRVT(tableId, i)) { continue; }
		err = selectRow(tableId, i, &load, &offset);
		ASSERT(err < 0, "select error");
		if (err == 0) {
			RowTag* tag = load->buff + offset;
			char* row = tag + 1;
			bool isMatch = TRUE;
			if (tk.whereStack) {
				isMatch = SqlHandleCalculateExpression(tableId, row, &tk.whereStack, getColumnValueByNameCB);
			}
			if (isMatch) {
				err = deleteRow(tableId, i); if (err) { ERROR(-1); }
				matchCnt++;
			}

			remainRowCnt--;
		}
		if (load)
			BuffRelease(&load);
	}
	
	if (matchCnt == 0) {
		return 1;
	}
	return 0;
error:
	BuffRelease(&load);

	return err;
}
static int showTables(uint64* tableId, uint32 cnt) {
	uint32 showCnt = 0;
	viewTableList(tableId, cnt);
	//if (tableId) {
	//	for (uint64 i = 0; i < cnt; i++, tableId++) {
	//		viewTableInfo(tableId[i]);
	//		showCnt++;
	//	}
	//}
	//else {
	//	// *
	//	for (uint64 i = 0; i < TABLE_CNT_MAX; i++) {
	//		if (gl_tableList[i].flag.isValid) {
	//			viewTableInfo(i);
	//			showCnt++;
	//		}
	//	}
	//}
	//if (showCnt == 0) {
	//	printm("*** no any tables ***\n");
	//}
	return 0;
}
static int showColumns(uint64 tableId, uint64* colList, uint32 cnt) {
	int err = 0;
	TableHandle* table = GET_TABLE_HANDLE(tableId);

	if (colList) {
		uint64* colId = colList;
		printm("****** table [%llu] %s ******\n", table->info->id, table->info->name);
		for (uint32 i = 0; i < cnt; i++) {
			printm("[");
			sqlCmdViewColumnInfo(table->info->colInfo + colId[i]);
			printm("]");
		}
		printm("\n");
	}
	else {
		//
		viewTableInfo(tableId);
	}
	
	return 0;
}

int initTableMgr() {
	return init();
}
int TableMgrInitRVT(uint64 tableId) {
	return initRVT(tableId);
}
uint64 TableMgrGetFreeTableId() {
	return getFreeTableId();
}
int TableMgrLoadTableInfo(uint64 tableId) {
	return loadTableInfo(tableId);
}
int TableMgrCreateTable(uint64 tableId, char name[TABLE_NAME_LEN_MAX], uint32 columnCnt, char* colInfoList) {
	return createTable(tableId, name, columnCnt, colInfoList);
}
int TableMgrDeleteTable(uint64 tableId) {
	return deleteTable(tableId);
}
void TableMgrCleanTable(uint64 tableId) {
	return cleanTable(tableId);
}
int TableMgrSetRCLock(uint64 tableId, uint64 rowId, uint64 colId) {
	return setRCLock(tableId, rowId, colId);
}
int TableMgrSetRowLock(uint64 tableId, uint64 rowId) {
	return setRowLock(tableId, rowId);
}
bool TableMgrCheckRowLock(uint64 tableId, uint64 rowId) {
	return checkRowLock(tableId, rowId);
}
void TableMgrCleanRCLock(uint64 tableId, uint64 rowId, uint64 colId) {
	return cleanRCLock(tableId, rowId, colId);
}
bool TableMgrCheckRCLock(uint64 tableId, uint64 rowId, uint64 colId) {
	return checkRCLock(tableId, rowId, colId);
}
void TableMgrCleanRowLock(uint64 tableId, uint64 rowId) {
	return cleanRowLock(tableId, rowId);
}
int TableMgrCreateRow(uint64 tableId, char* dataList, uint64* sizeList) {
	return createRow(tableId, dataList, sizeList);
}
int TableMgrInsertRow(uint64 tableId, SqlInsertColumnInfo* colList, uint64 len) {
	return insertRow(tableId, colList, len);
}
int TableMgrDeleteRow(uint64 tableId, uint64 rowId) {
	return deleteRow(tableId, rowId);
}
int TableMgrUpdateColumn(uint64 tableId, uint64 rowId, uint64 colId, char* data, uint64 size) {
	return updateColumn(tableId, rowId, colId, data, size);
}
int TableMgrUpdateColumnToRowBuffer(uint64 tableId, char* row, uint64 colId, char* data, uint64 size) {
	return updateColumnToRowBuffer(tableId, row, colId, data, size);
}
int TableMgrGetDestRowIdListForUpdate(uint64 tableId, SqlTokenHandle tk, BufferListInfo** rowList) {
	return getDestRowListForUpdate(tableId, tk, rowList);
}
int TableMgrShowTables(uint64* tableId, uint32 cnt) {
	return showTables(tableId, cnt);
}
int TableMgrShowColumns(uint64 tableId, uint64* colList, uint32 cnt) {
	return showColumns(tableId, colList, cnt);
}
TableHandle* TableMgrGetTableList(uint64 tableId) {
	return getTableList(tableId);
}
uint64 TableMgrFindNextFreeRow(uint64 tableId, uint64 startIndex) {
	return findNextFreeRow(tableId, startIndex);
}
int TableMgrSetRVT(uint64 tableId, uint64 rowId) {
	return setRVT(tableId, rowId);
}
void TableMgrCleanRVT(uint64 tableId, uint64 rowId) {
	return cleanRVT(tableId, rowId);
}
bool TableMgrCheckRVT(uint64 tableId, uint64 rowId) {
	return checkRVT(tableId, rowId);
}

uint64 TableMgrGetMaxValidRowIdByRVT(uint64 tableId) {
	return getMaxValidRowIdByRVT(tableId);
}
uint64 TableMgrGetColIdByName(uint64 tableId, char* columnName, uint32 len) {
	return getColIdByName(tableId, columnName, len);
}
char* TableMgrGetColValuePtrById(uint64 tableId, char* row, uint64 colId) {
	return getColValuePtrById(tableId, row, colId);
}
uint64 TableMgrGetTableIdByName(char* name, uint32 len) {
	return getTableIdByName(name, len);
}
uint64 TableMgrGetColumnIdListTotalValueSize(uint64 tableId, uint64* colIdList, uint32 cnt) {
	return getColumnIdListTotalValueSize(tableId, colIdList, cnt);
}
int TableMgrGetColumnValueByNameCB(uint64 tableId, char* row, char* columnName, uint32 len, BufferListInfo** outValue) {
	return getColumnValueByNameCB(tableId, row, columnName, len, outValue);
}
int TableMgrStrTableNameListToTableIdList(BufferListInfo** strTableList) {
	return strTableNameListToTableIdList(strTableList);
}
int TableMgrStrColumnListToColumnIdList(uint64 tableId, BufferListInfo** strColList) {
	return strColumnListToColumnIdList(tableId, strColList);
}
int TableMgrInitRowBloomFilterByTable(uint64 tableId) {
	return initRowBloomFilterByTable(tableId);
}
int TableMgrSetRowBloomFilter(uint64 tableId, uint64 rowId) {
	return setRowBloomFilter(tableId, rowId);
}
void TableMgrCleanRowBloomFilter(uint64 tableId, uint64 rowId) {
	return cleanRowBloomFilter(tableId, rowId);
}
bool TableMgrCheckRowBloomFilter(uint64 tableId, uint64 rowId) {
	return checkRowBloomFilter(tableId, rowId);
}

void TableMgrViewTableNameList(uint64 tableId) {
	return viewTableNameList(tableId);
}
void TableMgrViewTableInfo(uint64 tableId) {
	return viewTableInfo(tableId);
}
void TableMgrViewRVT(uint64 tableId) {
	return viewRVT(tableId);
}
void TableMgrViewTotalTable(uint64 tableId) {
	return viewTotalTable(tableId);
}
void TableMgrViewColumn(uint64 tableId, uint64 columnId, char* value) {
	return viewColumn(tableId, columnId, value);
}
void TableMgrViewRow(uint64 tableId, char* data) {
	return viewRow(tableId, data);
}
void TableMgrViewSelectOutput(SqlTokenHandle tk, BufferListInfo* output) {
	return viewSelectOutput(tk, output);
}
void TableMgrViewTableList(uint64* tableId, uint32 cnt) {
	return viewTableList(tableId, cnt);
}

int tableManager_test_input() {
	int err = 0;

	err = initBufferManagserBySize(1, BUFFER_POOL, BYTE_1G); ASSERT(err, "init buffer manager fail");
	err = initStorageEngine("c:\\tmp\\"); ASSERT(err, "init StorageEngine fail");
	err = initChangeLog();
	
	err = init(); ASSERT(err, "init buffer manager fail");
	
	//viewTotalTable(0);

	/*
	char select[256] = "select col1,col2 from table1 where userName < 'st r2' & (id = 6 | username2 = '123')";//"select col1,col2 from table1 where userName < 'str2' & ('col2' < 'col3' | id = 6)";
	char create[256] = "create table table1 ( col1 int8 index, col2 string(20), col3 float )";
	char update[256] = "update table1 set id = id+id2, id2 = 25 where id > id2";
	char insert[256] = "insert into table1 (userName, id,point) value ('str1', 123, 3.14)";
	char delete[256] = "delete from table2 where id > 80";
	char drop[256] = "drop table table1";
	*/

	char sql[256] = {0};
	gl_fileReadCnt = gl_fileWriteCnt = 0;

	while (1) {
		memset(sql, 0, sizeof(sql));
		printm("sql$: ");
		keybordInput(sql, 256);
		if (strncmp("quit", sql, 4) == 0) { break; }
		else if (sql[0] == 0 || sql[0] == '\n') { continue; }

		//client part
		char* sqlStream = NULL;
		uint32 sqlStreamLen = 0;

		err = sqlCmdByShell(sql, strlen(sql), &sqlStream, &sqlStreamLen);//453us
		//printm("%lluus\n", myCounterUs(time));//453us
		if (err) {
			printm("make stream fail\n");
			continue;
		}
		showSqlStream(sqlStream, sqlStreamLen); printm("\n");

		//host part
		SqlTokenHandle cmd = { 0 };

		err = SqlHandleDecodeStream(sqlStream, sqlStreamLen, &cmd); if (err) { printm("decode fail\n"); return; }

		SqlHandleViewTokens(&cmd);
		
		BufferListInfo* out = NULL;
		uint64 tableId = INVALID_TABLE_ID;
		switch (cmd.type) {
		case SQL_CMD_SELECT: {
			uint64 start = myCounterStart();
			tableId = getTableIdByName(cmd.tableName->buff, cmd.tableName->size);
			if (tableId == INVALID_TABLE_ID) {
				printm("invalid table\n");
				break;
			}
			err = strColumnListToColumnIdList(tableId, &cmd.destColList); ASSERT(err,"strColumnListToColumnIdList fail");

			err = selectRowList_ScanTable(tableId, cmd, &out);
			if (err == 1) {
				ChangeLogUpdateBackgroundLogToStorage();
				ChangeLogCheckpointBegin();
				ChangeLogCheckpointDone();
				err = selectRowList_ScanTable(tableId, cmd, &out);
			}
			uint64 us = myCounterUs(start);
			//10row -> 1509us

			//10000row -> 742519us

			ASSERT(err, "select by scan fail");
			if (!out) {
				printm("no match data\n");
			}
			else {
				viewSelectOutput(cmd, out);
			}

			printm("select [%llu us] read[%llu] write[%llu]\n", us, gl_fileReadCnt, gl_fileWriteCnt);
			break;
		}
		case SQL_CMD_UPDATE: {
			uint64 start = myCounterStart();
			tableId = getTableIdByName(cmd.tableName->buff, cmd.tableName->size);
			if (tableId == INVALID_TABLE_ID) {
				printm("invalid table\n");
				break;
			}
			BufferListInfo* rowList = NULL;
			
			err = TableMgrGetDestRowIdListForUpdate(tableId, cmd, &rowList);
			if (err == 2) {
				ChangeLogUpdateBackgroundLogToStorage();
				ChangeLogCheckpointBegin();
				ChangeLogCheckpointDone();
				err = TableMgrGetDestRowIdListForUpdate(tableId, cmd, &rowList);
			}
			ASSERT(err < 0, "get row list for update fail\n");
			if (err) {
				printm("no match dest row for update\n");
				break;
			}
			uint64 cnt = rowList->size / sizeof(uint64);
			uint64* rowId = rowList->buff;
			BufferListInfo** setExp = cmd.setList->buff;
			for (uint64 i = 0; i < cnt;i++, rowId++, setExp++) {
				BufferListInfo* load = NULL;
				uint64 offset = 0;
				err = selectRow(tableId, *rowId, &load, &offset); if (err) { printm("load row fail when update\n"); break; }
				RowTag* tag = load->buff;
				char* row = tag + 1;
				BufferListInfo* destCol = NULL;
				BufferListInfo* value = NULL;
				
				err = SqlHandleCalculateSetExpression(tableId, row, setExp, &destCol, &value, getColumnValueByNameCB); if (err) { printm("calculate set exp fail when update\n");
				ASSERT(TRUE, "");
				BuffRelease(&load); 
				break; }
				valueInfo* colNameInfo = destCol->buff;
				char* colName = colNameInfo + 1;
				uint64 colId = getColIdByName(tableId, colName, destCol->size - sizeof(valueInfo));
				valueInfo* info = value->buff;
				char* data = info + 1;
				uint32 dataSize = value->size - sizeof(valueInfo);
				uint32* num = data;
				
				err = TableMgrUpdateColumn(tableId, *rowId, colId, data, dataSize); 
				BuffRelease(&load);
				if (err) { 
					printm("update column fail\n"); 
					ASSERT(TRUE, "");
				break; }

			}
			uint64 us = myCounterUs(start);
			printm("update [%llu us] read[%llu] write[%llu]\n", us, gl_fileReadCnt, gl_fileWriteCnt);
			break;
		}
		case SQL_CMD_CREATE:{
			//create table table1 ( col1 int8 index, col2 string(20), col3 float )
			tableId = getFreeTableId();
			err = TableMgrCreateTable(tableId, cmd.tableName->buff, cmd.columnInfoList->size / sizeof(SqlColumnItem), cmd.columnInfoList->buff);
			if (err) {
				printm("create table fail err[%d]\n", err);
			}
			else {
				err = loadTableInfo(tableId); ASSERT(err, "load table info fail");
				TableHandle* table = GET_TABLE_HANDLE(tableId);
				err = SqlDecodeSetTableInfo(tableId, table->info, sizeof(TableInfo) + table->info->colNum * sizeof(SqlColumnItem));
				ASSERT(err, "set decode module table info fail");
				viewTableInfo(tableId);
			}
			break;
		}
		case SQL_CMD_INSERT: {
			tableId = getTableIdByName(cmd.tableName->buff, cmd.tableName->size);
			if (tableId == INVALID_TABLE_ID) {
				printm("invalid table\n");
				break;
			}
			err = TableMgrInsertRow(tableId, cmd.columnInfoList->buff, cmd.columnInfoList->size);
			ASSERT(err, "insert fail");
			break;
		}
		case SQL_CMD_DELETE:{
			tableId = getTableIdByName(cmd.tableName->buff, cmd.tableName->size);
			if (tableId == INVALID_TABLE_ID) {
				printm("invalid table\n");
				break;
			}
			err = deleteRow_ScanTable(tableId, cmd); ASSERT(err < 0, "delete fail");
			if (err == 1) { printm("delete row fail: not any row match\n"); }
			break;
		}
		case SQL_CMD_DROP: {
			tableId = getTableIdByName(cmd.tableName->buff, cmd.tableName->size);
			if (tableId == INVALID_TABLE_ID) {
				printm("invalid table\n");
				break;
			}
			err = TableMgrDeleteTable(tableId);
			ASSERT(err, "drop table fail");
			break;
		}
		case SQL_CMD_SHOW_TABLES: {
			err = strTableNameListToTableIdList(&cmd.destTableList);
			ASSERT(err, "tableNameList to tableIdList fail when show tables");
			if (cmd.destTableList) {
				err = TableMgrShowTables(cmd.destTableList->buff, cmd.destTableList->size / sizeof(uint64));
			}
			else {
				err = TableMgrShowTables(NULL, 0);
			}

			ASSERT(err, "show tables fail");
			break;
		}
		case SQL_CMD_SHOW_COLUMNS: {
			uint32 colCnt = 0;
			tableId = getTableIdByName(cmd.tableName->buff, cmd.tableName->size);
			if (tableId == INVALID_TABLE_ID) {
				printm("invalid table\n");
				break;
			}
			if (cmd.destColList) {
				err = strColumnListToColumnIdList(tableId, &cmd.destColList); ASSERT(err, "strColumnListToColumnIdList fail");
				colCnt = cmd.destColList->size / sizeof(uint64);
			}
			if (cmd.destColList) {
				err = TableMgrShowColumns(tableId, cmd.destColList->buff, colCnt); ASSERT(err, "show columns fail");
			}
			else {
				err = TableMgrShowColumns(tableId, NULL, 0); ASSERT(err, "show columns fail");
			}
			
			break;
		}
		default: {
			return -1;
		}
		}

		BuffRelease(&out);
		SqlHandleFreeTokens(&cmd);
		gl_fileReadCnt = 0;
		gl_fileWriteCnt = 0;
	}
	
}

void tableManager_test1() {
	int err = 0;

	err = initBufferManagserBySize(1, BUFFER_POOL, BYTE_1G); ASSERT(err, "init buffer manager fail");
	err = initStorageEngine("c:\\tmp\\"); ASSERT(err, "init StorageEngine fail");
	err = initChangeLog();
	err = init(); ASSERT(err, "init buffer manager fail");

	uint64 tableId = 0;
	SqlColumnItem colList[2] = { 
		{ 
			.dataType = SQL_DATA_INT32, 
			.name = "COL1"
		},
		{
			.dataType = SQL_DATA_STRING, 
			.strMaxLen = 256, 
			.name = "COL2"
		}
	};
	err = createTable(tableId, "table1", 2, colList);
	//ASSERT(err, "create table fail");
	if (err == 0) { err = loadTableInfo(tableId); ASSERT(err, "load table info fail");
	}

	TableHandle* table = GET_TABLE_HANDLE(tableId);

	int num = 0;
	if (!err) {
		viewTableInfo(tableId);
		for (uint32 i = 0; i < 10000; i++) {
			char dataList[(4 + 256)] = { 0 };
			int* col1 = dataList;	*col1 = num++;
			char* col2 = col1 + 1;	memcpy(col2, "abcdefg", strlen("abcdefg"));
			uint64 sizeList[2] = { 4, strlen("abcdefg") };
			err = createRow(tableId, dataList, sizeList);
			ASSERT(err < 0, "create row fail");
			if (err == 1) {
				ChangeLogUpdateBackgroundLogToStorage();
				ChangeLogCheckpointBegin();
				ChangeLogCheckpointDone();
			}
		}
	}
	
	viewTotalTable(tableId);

	for (uint32 i = 0; i < 10; i++) {
		BufferListInfo* load = NULL;
		uint64 offset = 0;
		err = selectRow(tableId, i, &load, &offset);
		if (err == 1) {
			ChangeLogUpdateBackgroundLogToStorage();
			ChangeLogCheckpointBegin();
			ChangeLogCheckpointDone();
			err = selectRow(tableId, i, &load, &offset); ASSERT(err, "select fail after checkpoint");
			viewRow(tableId, load->buff + offset + sizeof(RowTag));
		}
		else if (err == 2) {
			//waite check point
			//never hear
		}
		else if (err == 0) {
			//pass
			viewRow(tableId, load->buff + offset + sizeof(RowTag));
		}
		else {
			
		}
		
		BuffRelease(&load);
	}
	err = deleteRow(tableId, 2);
	if (err == 1) {
		ChangeLogUpdateBackgroundLogToStorage();
		ChangeLogCheckpointBegin();
		ChangeLogCheckpointDone();
	}
	viewTotalTable(tableId);
	{
		BufferListInfo* load = NULL;
		uint64 offset = 0;
		err = selectRow(tableId, 2, &load, &offset);
		if (err == 1 || err == 2 || err == -2) {
			ASSERT(TRUE, "select deleted row but not fail?");
		}
		BuffRelease(&load);
	}

	for (uint32 i = 3; i < 5;i++) {
		err = updateColumn(tableId, i, 1, "PWXOIZ", strlen("PWXOIZ"));
		ASSERT(err < 0, "update col fail[%d]", err);
		if (err == 1) {
			ChangeLogUpdateBackgroundLogToStorage();
			ChangeLogCheckpointBegin();
			ChangeLogCheckpointDone();
			printm("flush change log\n");
		}

	}
	viewTotalTable(tableId);

	for (uint32 i = 0; i < 10; i++) {
		BufferListInfo* load = NULL;
		uint64 offset = 0;
		err = selectRow(tableId, i, &load, &offset);
		if (err == 1) {
			ChangeLogUpdateBackgroundLogToStorage();
			ChangeLogCheckpointBegin();
			ChangeLogCheckpointDone();
			err = selectRow(tableId, i, &load, &offset); ASSERT(err, "select fail after checkpoint");
			viewRow(tableId, load->buff + offset + sizeof(RowTag));
		}
		else if (err == 2) {
			//waite check point
		}
		else if (err == 0) {
			//pass
			viewRow(tableId, load->buff + offset + sizeof(RowTag));
		}
		else {
			//invalid row
		}
		
		BuffRelease(&load);
	}


	err = deleteTable(tableId);

	ASSERT(err,"delete table fail");

	printm("pass");
}
void tableManager_test2() {
	int err = 0;

	err = initBufferManagserBySize(1, BUFFER_POOL, BYTE_1G); ASSERT(err, "init buffer manager fail");
	err = initStorageEngine("c:\\tmp\\"); ASSERT(err, "init StorageEngine fail");
	err = initChangeLog();
	
	err = init(); ASSERT(err, "init buffer manager fail");
	
	//viewTotalTable(0);

	TableHandle* table = GET_TABLE_HANDLE(0);
	err = SqlDecodeSetTableInfo(0, table->info, sizeof(TableInfo) + table->info->colNum*sizeof(SqlColumnItem));
	ASSERT(err, "set decode module table info fail");
	char select[256] = "select * from table1 where COL1>3 &COL1<=5|COL1>7&COL1<9";

	char* cmdStr = select;
	//client part
	char* sqlStream = NULL;
	uint32 sqlStreamLen = 0;

	err = sqlCmdByShell(cmdStr, strlen(cmdStr), &sqlStream, &sqlStreamLen);//453us
	//printm("%lluus\n", myCounterUs(time));//453us
	if (err) {
		printm("make stream fail\n"); return;
	}
	showSqlStream(sqlStream, sqlStreamLen); printm("\n");
	
	
	//host part
	SqlTokenHandle cmd = { 0 };

	err = SqlHandleDecodeStream(sqlStream, sqlStreamLen, &cmd); if (err) { printm("decode fail\n"); return; }

	SqlHandleViewTokens(&cmd);

	err = strTableToId(&cmd.tableName);
	uint64* tableId = cmd.tableName->buff;
	err = strColumnListToColumnIdList(*tableId, &cmd.destColList);

	BufferListInfo* out = NULL;
	err = selectRowList_ScanTable(0, cmd, &out);
	//10row -> 1509us
	//10000row -> 742519us

	ASSERT(err, "select by scan fail");
	if (!out) {
		printm("no match data\n");
	}
	else {
		viewSelectOutput(cmd, out);
	}
	

	return;
}
