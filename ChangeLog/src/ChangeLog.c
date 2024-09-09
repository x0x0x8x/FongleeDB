#include "ChangeLog.h"
#include "FileIO.h"

#include "MyStrTool.h"
#include "MyTime.h"
#include "MyTool.h"
#include "DiskLog.h"

/*
Be use for FongLeeDB's update change log.

two file A and B.

First recv new update command and data into fileA when fileA have not full.
And change A and B when A full, then B will be A , A will be B(Logical exchange).
Then, continue recv new update command to fileA(previous fileB), and fileB will flush to real table file.

*/

#define ERROR(errCode) {err = errCode; goto error;}
#ifdef DEBUG_CHANGE_LOG
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_CHANGE_LOG

#define CHANGE_LOG_LIST_CNT_MAX_PER_FILE (256U)
#define CHANGE_LOG_THRESHOLD_SWAP_SIZE_REAL(pool) (\
MIN(CHANGE_LOG_THRESHOLD_SWAP_SIZE, \
BuffGetPoolSize(pool) - BuffGetUsedSizeByPool(pool) - 8))
#define CHANGE_LOG_CACHE_SIZE_MIN (MAX(BYTE_1K*4, FILEIO_GetBlockSize()))

#define BUFFER_POOL CAHNGE_LOG_BUFFER_POOL

typedef struct logItemInfo_t {
	uint64 seek;
	uint64 size;
	uint64 seekInLog;
}LogItemInfo;
typedef struct fileIdListItem_t {
	BufferListInfo* logList;//LogItemInfo logList[CHANGE_LOG_LIST_CNT_MAX_PER_FILE];//CHANGE_LOG_LIST_BLOCK_CNT
	uint32 freeIndex;
	uint64 dataTotalSize;
}fileIdListItem;

typedef struct LogFileHead_t {
	uint64 time;
}LogFileHead;
typedef enum LogState_e {
	LOG_STATE_EMPTY,
	LOG_STATE_NOT_FULL,	//inserting
	LOG_STATE_FULL,	//need check-point
	LOG_STATE_CHECKPOINTING,
	LOG_STATE_CHECKPOINT_DONE,

}LogState;
typedef struct LogFlag_t {
	uint8 state : 3;	//LogState
	uint8 isReading : 1;
}LogFlag;
typedef struct LogWriteCache_t {
	uint64 headSeek;//c1 head seek
	uint64 seek;//seek in cache1
	BufferListInfo* c1;
	BufferListInfo* c2;
}LogWriteCache;
typedef struct Log_t {
	uint64 logFileId;
	LogFlag flag;
	FILE_HANDLE f;
	FILE_HANDLE m;
	char* ptr;
	LogFileHead* head;
	char* cur;
	uint64 mapsize;
	
	fileIdListItem fileIdList[CHANGE_LOG_FILE_ID_MAX];//fileIdListItem fileIdList[2048];
	LogWriteCache cache;
	uint64 logSize;
	Mutex readMutex;
}Log;

typedef struct logItem_t {
	logItemType type;
	uint32 size;
	uint64 fileId;
	int64 seek;
	//char* data
	//char* splitLine
}logItem;
extern bool CheckPointTaskIsIdle();
extern uint64 gl_fileReadCnt;
extern uint64 gl_fileWriteCnt;
static Log gl_logs[2] = { 0 };
static Log* gl_FLog = gl_logs + 0;
static Log* gl_BLog = gl_logs + 1;
static LogCommandItem gl_queue[CHANGE_LOG_COMMAND_QUEUE_LEN_MAX] = {0};
static uint32 gl_qHead = 0;
static uint32 gl_qTail = 0;
static Mutex gl_commandQueuePushMutex;
static Mutex gl_commandQueuePopMutex;
static Mutex gl_mutex;
#define CMD_QUEUE_INDEX(index) (index % CHANGE_LOG_COMMAND_QUEUE_LEN_MAX) 

static int pushCmd(uint64 fileId, logItemType type, uint64 seek, BufferListInfo* data, uint64 offset, uint64 size, int* resp, void* callback) {
	if (size == 0 && type == LOG_TYPE_WRITE) {
		return -1;
	}
	lockMutex(&gl_mutex);
	LogCommandItem* item = gl_queue + CMD_QUEUE_INDEX(gl_qTail);
	if (CMD_QUEUE_INDEX(gl_qTail) == CMD_QUEUE_INDEX(gl_qHead) && item->size != 0) {
		unlockMutex(&gl_mutex);
		return -1;//full
	}
	
	ASSERT(item->size > 0,"not empty queue command\n");
	item->fileId = fileId;
	item->seek = seek;
	item->data = data;
	item->offset = offset;
	item->size = size;
	item->resp = resp;
	item->callback = callback;
	gl_qTail++;

	unlockMutex(&gl_mutex);
	return 0;
}
static int popCmd(LogCommandItem* outItem) {
	lockMutex(&gl_mutex);
	LogCommandItem* item = gl_queue + CMD_QUEUE_INDEX(gl_qHead);
	if (item->callback || item->resp) {
		memcpy(outItem, item, sizeof(LogCommandItem));
		memset(item, 0, sizeof(LogCommandItem));
		gl_qHead++;
		unlockMutex(&gl_mutex);
		return 1;
	}
	unlockMutex(&gl_mutex);
	return 0;
}

static int extendLogList(BufferListInfo** logList, uint32 appendCnt) {
	int err = 0;
	if (*logList == NULL) {
		*logList = getBuffer(ROUND_UP(CHANGE_LOG_LIST_BLOCK_CNT, appendCnt) * sizeof(LogItemInfo), BUFFER_POOL);
	}
	else{
		uint64 destSize = (*logList)->size + ROUND_UP(CHANGE_LOG_LIST_BLOCK_CNT, appendCnt) * sizeof(LogItemInfo);
		err = BuffRealloc(logList, destSize, (*logList)->poolId); if (err) { return -1; }
	}
	return 0;
}
static int initLogList(Log* log, uint64 fileId) {
	BuffRelease(&log->fileIdList[fileId].logList);
	log->fileIdList[fileId].logList = getBuffer(CHANGE_LOG_LIST_BLOCK_CNT * sizeof(LogItemInfo), BUFFER_POOL); if (!log->fileIdList[fileId].logList) { return -1; }
	log->fileIdList[fileId].dataTotalSize = 0;
	log->fileIdList[fileId].freeIndex = 0;
	return 0;
}
static int removeLogFile() {
	int err = 0;
	char addr[FILE_ADDR_MAX_LEN] = { 0 };
	err = StorageGetFilePathById(CHANGE_LOG_FILE_ID_A, addr);	if (err) { return -1; }
	err = FILEIO_Remove(addr); if (err) { return -1; }

	memset(addr, 0, sizeof(addr));
	err = StorageGetFilePathById(CHANGE_LOG_FILE_ID_B, addr);	if (err) { return -1; }
	err = FILEIO_Remove(addr); if (err) { return -1; }

	return 0;
}
static int cleanLog(Log* log) {
	int err = 0;
	//reset
	ASSERT(log->flag.isReading, "clean log when reading\n");
	err = FILEIO_ResetFileSize(log->f, CHANGE_LOG_BLOCK_SIZE);
	uint64 newSize = FILEIO_GetFileSize(log->f);
	BufferListInfo* tmp = getBuffer(ROUND_UP(sizeof(LogFileHead) + sizeof(logItem), FILEIO_GetBlockSize()), BUFFER_POOL); if (!tmp) { return -2; }
	err = FILEIO_Write(log->f, tmp->buff, tmp->size); if (err) { BuffRelease(&tmp); return -1; }
	BuffRelease(&tmp);
	for (uint32 i = 0; i < CHANGE_LOG_FILE_ID_MAX; i++) {
		BuffRelease(&log->fileIdList[i].logList);
	}
	memset(log->fileIdList, 0, sizeof(fileIdListItem)* CHANGE_LOG_FILE_ID_MAX);
	
	if (log->cache.c1) {
		memset(log->cache.c1->buff, 0, log->cache.c1->size);
	}
	BuffRelease(&log->cache.c2);
	log->cache.headSeek = log->cache.seek = 0;
	log->logSize = 0;
	memset(&log->flag, 0, sizeof(LogFlag));//mast be final

	return 0;
}
static int swap() {
	if (gl_BLog->flag.state != LOG_STATE_EMPTY) {
		return -1;
	}
	//printm("swap\n");
	if (gl_FLog == gl_logs + 0) {
		gl_FLog = gl_logs + 1;
		gl_BLog = gl_logs + 0;
	}
	else {
		gl_FLog = gl_logs + 0;
		gl_BLog = gl_logs + 1;
	}
	gl_BLog->flag.state = LOG_STATE_FULL;

	return 0;
}
static int updateBackgroundLogToStorage() {
	//printm("flush\n");
	ASSERT(gl_BLog->flag.state != LOG_STATE_FULL, "BLog not full\n");
	int err = 0;
	uint64 curFlushingTotalSize = 0;
	uint32 cnt = sizeof(gl_BLog->fileIdList) / sizeof(gl_BLog->fileIdList[0]);
	for (uint32 i = 0; i < cnt;i++) {
		if (gl_BLog->fileIdList[i].freeIndex > 0) {
			BufferListInfo* seekList = getBuffer(gl_BLog->fileIdList[i].freeIndex * sizeof(uint64), BUFFER_POOL);
			BufferListInfo* sizeList = getBuffer(gl_BLog->fileIdList[i].freeIndex * sizeof(uint64), BUFFER_POOL);
			BufferListInfo* dataList = getBuffer(gl_BLog->fileIdList[i].dataTotalSize, BUFFER_POOL);//it is may out of pool size range when log too long!
			if (!seekList || !sizeList || !dataList) {
				ASSERT(TRUE, "buffer not enough");
				return -2;
			}
			uint64* seek = seekList->buff;
			uint64* size = sizeList->buff;
			char* data = dataList->buff;
			for (uint32 j = 0; j < gl_BLog->fileIdList[i].freeIndex;j++, data+=*size, seek++, size++) {
				LogItemInfo* logList = gl_BLog->fileIdList[i].logList->buff;
				*seek = logList[j].seek;
				*size = logList[j].size;	curFlushingTotalSize += logList[j].size;
				//memcpy(data, gl_BLog->ptr + logList[j].seekInLog, *size);
				BufferListInfo* load = NULL;
				uint64 offset = 0;
				err = StorageRead(gl_BLog->logFileId, logList[j].seekInLog, *size, &load, &offset); if (err) { 
					BuffRelease(&load);
					return -2; }
				ASSERT(*size > load->size - offset, "out of range\n");
				memcpy(data, load->buff+offset, *size);
				BuffRelease(&load);
			}
			err = StoragePushCommand(i, gl_BLog->fileIdList[i].freeIndex, seekList, dataList, sizeList);
			if (err) { return -2; }
		}
	}
	//printm("check point total size: %llu\n", curFlushingTotalSize);
	return 0;
}
static uint64 lastRemain = 0;
static int write_dist_prepare(uint64 writingSize) {
#define REMAIN_IN_CACHE1 ((gl_FLog->cache.headSeek == 0 && gl_FLog->logSize == 0)?(gl_FLog->cache.c1->size - gl_FLog->cache.seek - sizeof(LogFileHead)):(gl_FLog->cache.c1->size - gl_FLog->cache.seek))
	if (REMAIN_IN_CACHE1 < writingSize + sizeof(logItem) && !gl_FLog->cache.c2) {
		//need load new cache
		ASSERT(gl_FLog->cache.c2, "Log cache2 not free\n");
		BufferListInfo* tmp = getBuffer(MAX(ROUND_UP(writingSize - REMAIN_IN_CACHE1, CHANGE_LOG_CACHE_SIZE_MIN), CHANGE_LOG_CACHE_SIZE_MIN), BUFFER_POOL); if (!tmp) { return -2; }
		gl_FLog->cache.c2 = tmp;
	}
	lastRemain = REMAIN_IN_CACHE1;
	return 0;
}
static int write_disk(uint64 fileId, logItemType type, FILE_HANDLE f, LogWriteCache* cache, uint64 seek, char* data, uint64 size) {
	int err = 0;
#define REMAIN_IN_CACHE1 (cache->c1->size - cache->seek)
	if (gl_FLog->logSize == 0 && gl_FLog->cache.headSeek == 0) {
		LogFileHead* head = cache->c1->buff;
		head->time = myCounterStart();
		cache->seek += sizeof(LogFileHead);
	}
	ASSERT(REMAIN_IN_CACHE1 != lastRemain, "REMAIN_IN_CACHE1 != lastRemain\n");
	ASSERT((REMAIN_IN_CACHE1 < size + sizeof(logItem))&&(!cache->c2), "buffer not ready1\n");
	ASSERT((REMAIN_IN_CACHE1 >= size + sizeof(logItem)) && (cache->c2), "buffer enough but c2 exist?\n");
	if (REMAIN_IN_CACHE1 < size) {
		if (cache->c2->size + REMAIN_IN_CACHE1 < size) {
			ASSERT(cache->c2->size + REMAIN_IN_CACHE1 < size, "buffer not enough in readyed\n");
			ERROR(-2);
		}		
	}
	
	logItem itemInfo = { 0 };
	itemInfo.type = type;
	itemInfo.fileId = fileId;
	itemInfo.seek = seek;
	itemInfo.size = size;
	char* ptr = cache->c1->buff + cache->seek;
	uint64 c2Seek = 0;
	
	if (REMAIN_IN_CACHE1 >= sizeof(logItem)) {
		memcpy(ptr, &itemInfo, sizeof(logItem));	ptr += sizeof(logItem);
		if (ptr - cache->c1->buff >= cache->c1->size) {
			ptr = cache->c2->buff;
		}
		cache->seek += sizeof(logItem);
	}
	else {
		uint64 size1 = REMAIN_IN_CACHE1;
		uint64 size2 = sizeof(logItem) - size1;
		memcpy(cache->c1->buff + cache->seek, &itemInfo, size1);	
		cache->seek += size1;
		memcpy(cache->c2->buff, ((char*)(&itemInfo)) + size1, size2);
		ptr = cache->c2->buff + size2;
		c2Seek += size2;
	}

	if (REMAIN_IN_CACHE1 > 0) {
		if (size <= REMAIN_IN_CACHE1) {
			if (data) {
				memcpy(ptr, data, size);
				ptr += size;
				cache->seek += size;
			}
		}
		else {
			uint64 size1 = REMAIN_IN_CACHE1;
			uint64 size2 = size - size1;
			if(data)
				memcpy(ptr, data, size1);

			ptr = cache->c2->buff;
			if(data)
				memcpy(ptr, data + size1, size2);
			ptr += size2;
			cache->seek += size1;
			c2Seek += size2;
		}
	}
	else {
		//ptr in cache2 now
		if(data)
			memcpy(ptr, data, size); ptr += size;
		c2Seek = ptr - cache->c2->buff;
	}
	err = FILEIO_MoveSeek(f, cache->headSeek, SEEK_SET); if (err) { ASSERT(err, "Move seek fail\n"); 
	ERROR(-3); }
	err = FILEIO_Write(f, cache->c1->buff, cache->c1->size); if (err) { ASSERT(err, "write file fail\n"); 
	ERROR(-4); }

	if (REMAIN_IN_CACHE1 == 0) {
		ASSERT(!cache->c2, "c2 is null\n");
		cache->headSeek += cache->c1->size;
		BuffRelease(&cache->c1);
		cache->c1 = cache->c2;
		cache->c2 = NULL;
		cache->seek = c2Seek;
		if (cache->seek > 0) {
			err = FILEIO_MoveSeek(f, cache->headSeek, SEEK_SET); if (err) { ASSERT(err, "Move seek fail\n"); 
			ERROR(-3); }
			err = FILEIO_Write(f, cache->c1->buff, cache->c1->size); if (err) { ASSERT(err, "write file fail\n"); 
			ERROR(-4); }
		}		
	}
	else {
		if (cache->c2) {
			ASSERT(cache->c2, "c2 is not null\n");
			ERROR(-5);
		}
		
	}

	return 0;

error:

	return err;
}
static int write_mamap_prepare(uint64 writingSize) {
	int err = 0;
	uint64 needMapSize = gl_FLog->cur + sizeof(logItem) + writingSize - gl_FLog->ptr;
	if (needMapSize > gl_FLog->mapsize) {
		uint64 lastSeek = gl_FLog->cur - gl_FLog->ptr;
		uint64 newMapSize = ROUND_UP(needMapSize, CHANGE_LOG_BLOCK_SIZE);
		err = FILEIO_MemoryReMap(gl_FLog->f, &gl_FLog->m, gl_FLog->mapsize, newMapSize, &gl_FLog->ptr);
		if (err) { return -1; }
		gl_FLog->cur = gl_FLog->ptr + lastSeek;
		gl_FLog->mapsize = newMapSize;
		gl_FLog->head = gl_FLog->ptr;
	}

	return 0;
}
static int write_mamap(uint64 fileId, uint64 seek, char* data, uint64 size) {
	int err = 0;
	logItem* item = gl_FLog->cur;
	item->fileId = fileId;
	item->seek = seek;
	item->size = size;
	memcpy(item + 1, data, size);

	FILEIO_MemoryFlush(item, sizeof(logItem) + size); gl_fileWriteCnt++;

	return 0;
}
static int prepareLogList(uint64 fileId, uint32 appendCnt) {
	int err = 0;
	uint32 curMaxCnt = gl_FLog->fileIdList[fileId].logList?gl_FLog->fileIdList[fileId].logList->size / sizeof(LogItemInfo):0;
	if (gl_FLog->fileIdList[fileId].freeIndex + appendCnt >= curMaxCnt) {
		err = extendLogList(&gl_FLog->fileIdList[fileId].logList, appendCnt);
	}
	return err;
}
static int flushLogPrepare(uint32 fileId, uint64 writingSize, uint32 appendCnt) {
	int err = 0;
	int ret = 0;
	if (gl_FLog->fileIdList[fileId].dataTotalSize + writingSize >= CHANGE_LOG_THRESHOLD_SWAP_SIZE_REAL(BUFFER_POOL) ||
		gl_FLog->fileIdList[fileId].freeIndex >= MIN(CHANGE_LOG_THRESHOLD_LOG_ITEM_CNT, CHANGE_LOG_LOG_CNT_MAX_PER_FILE)
		) {
		err = swap();
		ret = 1;//need flush
		if (gl_FLog->fileIdList[fileId].dataTotalSize + writingSize >= CHANGE_LOG_THRESHOLD_SWAP_SIZE_REAL(BUFFER_POOL) || 
			gl_FLog->fileIdList[fileId].freeIndex >= CHANGE_LOG_LOG_CNT_MAX_PER_FILE
			) {
			return 2;
		}
	}
	err = prepareLogList(fileId, appendCnt);
	if (err) { return err; };
	err = write_dist_prepare(writingSize);
	if (err) { return err; };
	
	return ret;
}
static int flushLog(uint64 fileId, logItemType type, uint64 seek, uint64 size, char* data) {
	//mast run alone
	//ASSERT(size > gl_FLog->mapsize, "Data is too long for change log!");
	int err = 0;
	ASSERT(!gl_FLog->fileIdList[fileId].logList, "Log list not init\n");
	ASSERT(gl_FLog->fileIdList[fileId].freeIndex >=(gl_FLog->fileIdList[fileId].logList->size / sizeof(LogItemInfo)), "Log list size not enough\n");
	//ASSERT(gl_FLog->fileIdList[fileId].freeIndex >= CHANGE_LOG_THRESHOLD_LOG_ITEM_CNT || gl_FLog->fileIdList[fileId].dataTotalSize >= CHANGE_LOG_THRESHOLD_SWAP_SIZE, "need swap prepare\n");
	ASSERT(gl_FLog->fileIdList[fileId].dataTotalSize + size >= CHANGE_LOG_THRESHOLD_SWAP_SIZE_REAL(BUFFER_POOL) ||
		gl_FLog->fileIdList[fileId].freeIndex >= CHANGE_LOG_LOG_CNT_MAX_PER_FILE
		, "buffer not enough if keep push\n");

	// write

	//err = write_mamap(fileId, seek, data, size);
	{
		err = write_disk(fileId, type, gl_FLog->f, &gl_FLog->cache, seek, data, size); if (err) { return -2; }
	}

	//append log ptr list
	uint32 freeIndex = gl_FLog->fileIdList[fileId].freeIndex;
	
	LogItemInfo* logList = gl_FLog->fileIdList[fileId].logList->buff;
	logList[freeIndex].seek = seek;
	logList[freeIndex].size = size;
	gl_FLog->fileIdList[fileId].dataTotalSize += size;

	if (gl_FLog->logSize == 0) {
		gl_FLog->logSize += sizeof(LogFileHead);
	}
	logList[freeIndex].seekInLog = gl_FLog->logSize + sizeof(logItem);
	gl_FLog->logSize += size + sizeof(logItem);
	gl_FLog->fileIdList[fileId].freeIndex++;
	gl_FLog->flag.state = LOG_STATE_NOT_FULL;
	
	return 0;
}

static bool isDestIdInLog(uint64 fileId) {
	if (gl_BLog->fileIdList[fileId].freeIndex > 0 ||
		gl_FLog->fileIdList[fileId].freeIndex > 0
		) {
		return TRUE;
	}
	else {
		return FALSE;
	}
}
static int readFromLog(Log* log, uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset) {
	int err = 0;
	if (log->fileIdList[fileId].freeIndex == 0) {
		return 2;
	}
	LogItemInfo* logList = log->fileIdList[fileId].logList->buff;
	for (uint32 i = log->fileIdList[fileId].freeIndex-1; log->fileIdList[fileId].logList;i--) {
		if (logList[i].size == 0) {
			//remove command
			break;
		}
		if (logList[i].seek > seek + size-1 || 
			logList[i].seek + logList[i].size - 1 < seek) {
			if (i == 0) { break; }
			continue;
		}
		else if (logList[i].seek <= seek && 
			logList[i].size >= size
			) {
			//read log
			err = StorageRead(log->logFileId, 
				logList[i].seekInLog + (seek - logList[i].seek),
				size, data, offset);
			return 0;
		}
		else {
			//need flush and read table
			return 1;
		}


		if (i == 0) { break; }
	}
	//not found
	return 2;
}
static int readLog(uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset) {
	int err = 0;
	if (!isDestIdInLog(fileId)) {
		return 2;
	}
	lockMutex(&gl_BLog->readMutex);
	ASSERT(!gl_BLog->flag.isReading, "not set flag: isReading\n");//ZBH ???
	err = readFromLog(gl_BLog, fileId, seek, size, data, offset); 
	unlockMutex(&gl_BLog->readMutex);
	if (err == 0) {
		//readed
		return 0;
	}
	else if (err == 1) {
		//need flush
		return 1;
	}
	else {
		//not found
	}
	err = readFromLog(gl_FLog, fileId, seek,size, data,offset);
	if (err == 0) {
		return 0;
	}
	else if (err == 1) {
		//need flush
		if (gl_BLog->flag.state != LOG_STATE_EMPTY) {
			//need wait last checkpoint done
			return -2;
		}
		err = swap();
		ASSERT(err, "swap fail");
		return 1;
	}
	else {
		//not found
	}

	return 2;
}

static bool needCheckPoint() {
	return gl_BLog->flag.state == LOG_STATE_FULL;
}
static int checkpointDone() {
	return cleanLog(gl_BLog);
}
static void checkpoint() {
	StorageDoCommand();
}
static int flushAllLog() {
	int err = 0;
	err = updateBackgroundLogToStorage(); if (err) { return -1; }
	checkpoint();
	err = checkpointDone(); if (err) { return -4; }

	err = swap(); if (err) { return -3; }
	err = updateBackgroundLogToStorage(); if (err) { return -2; }
	checkpoint();
	err = checkpointDone(); if (err) { return -4; }

	return 0;
}
static int flushAllLogWhenBoot() {
	int err = 0;
	
	//logF
	uint64 size = MAX(FILEIO_GetFileSize(gl_FLog->f), CHANGE_LOG_BLOCK_SIZE);
	err = FILEIO_MemoryMap(gl_FLog->f, &gl_FLog->m, size, &gl_FLog->ptr);
	gl_FLog->head = gl_FLog->ptr;
	gl_FLog->cur = gl_FLog->head + 1;
	gl_FLog->mapsize = size;

	//logB
	size = MAX(FILEIO_GetFileSize(gl_BLog->f), CHANGE_LOG_BLOCK_SIZE);
	err = FILEIO_MemoryMap(gl_BLog->f, &gl_BLog->m, size, &gl_BLog->ptr);
	gl_BLog->head = gl_BLog->ptr;
	gl_BLog->cur = gl_BLog->head + 1;
	gl_BLog->mapsize = size;
	
	if (gl_BLog->head->time > gl_FLog->head->time) {
		Log* tmp = gl_FLog;
		gl_FLog = gl_BLog;
		gl_BLog = tmp;
	}

	//flush
	LogFileHead* head = gl_BLog->ptr;
	logItem* item = head + 1;
	char* data = item + 1;
	printm("*** Change Log reboot ***\n");
	printm("Log state is [%d]\n", checkLogState());//ZBH error value???
	while ((char*)item - gl_BLog->ptr < gl_BLog->mapsize && (item->size > 0 || item->size == 0 && item->type == LOG_TYPE_REMOVE)) {
		BufferListInfo* seekList = NULL;
		BufferListInfo* sizeList = NULL;
		BufferListInfo* dataList = NULL;
		if (item->type == LOG_TYPE_REMOVE) {
			printm("remove id[%llu]\n", item->fileId);
		}
		else {
			seekList = getBuffer(sizeof(uint64), BUFFER_POOL);	if (!seekList) { ASSERT(TRUE, "buffer not enough\n"); return -1; }
			sizeList = getBuffer(sizeof(uint64), BUFFER_POOL);	if (!sizeList) { ASSERT(TRUE, "buffer not enough\n"); return -1; }
			dataList = getBuffer(item->size, BUFFER_POOL);		if (!dataList) { ASSERT(TRUE, "buffer not enough\n"); return -1; }
			uint64* seek = seekList->buff;	*seek = item->seek;
			uint64* size = sizeList->buff;	*size = item->size;
			memcpy(dataList->buff, data, item->size);
			printm("write id[%llu] seek[%llu] data[%.*s]\n", item->fileId, *seek, *size, data);
		}
		
		
		err = StoragePushCommand(item->fileId, 1, seekList, dataList, sizeList);
		if (err) { 
			StorageDoCommand();
			err = StoragePushCommand(item->fileId, 1, seekList, dataList, sizeList);
			if (err) {
				return -2;
			}
		}

		item = data + item->size;
		data = item + 1;
	}
	StorageDoCommand();

	head = gl_FLog->ptr;
	item = head + 1;
	data = item + 1;
	while ((char*)item - gl_FLog->ptr < gl_FLog->mapsize && (item->size > 0 || item->size == 0 && item->type == LOG_TYPE_REMOVE)) {
		BufferListInfo* seekList = NULL;
		BufferListInfo* sizeList = NULL;
		BufferListInfo* dataList = NULL;
		if (item->type == LOG_TYPE_REMOVE) {
			printm("remove id[%llu]\n", item->fileId);
		}
		else {
			seekList = getBuffer(sizeof(uint64), BUFFER_POOL);	if (!seekList) { ASSERT(TRUE, "buffer not enough\n"); return -1; }
			sizeList = getBuffer(sizeof(uint64), BUFFER_POOL);	if (!sizeList) { ASSERT(TRUE, "buffer not enough\n"); return -1; }
			dataList = getBuffer(item->size, BUFFER_POOL);		if (!dataList) { ASSERT(TRUE, "buffer not enough\n"); return -1; }
			uint64* seek = seekList->buff;	*seek = item->seek;
			uint64* size = sizeList->buff;	*size = item->size;
			memcpy(dataList->buff, data, item->size);
			printm("write id[%llu] seek[%llu] data[%.*s]\n", item->fileId, *seek, *size, data);
		}
		err = StoragePushCommand(item->fileId, 1, seekList, dataList, sizeList);
		if (err) {
			StorageDoCommand();
			err = StoragePushCommand(item->fileId, 1, seekList, dataList, sizeList);
			if (err) {
				return -2;
			}
		}

		item = data + item->size;
		data = item + 1;
	}
	StorageDoCommand();
	memset(gl_FLog->ptr, 0, gl_FLog->mapsize);
	memset(gl_BLog->ptr, 0, gl_BLog->mapsize);
	FILEIO_MemoryMapClose(gl_FLog->m, gl_FLog->ptr, gl_FLog->mapsize);
	FILEIO_MemoryMapClose(gl_BLog->m, gl_BLog->ptr, gl_BLog->mapsize);
	FILEIO_ResetFileSize(gl_FLog->f, CHANGE_LOG_BLOCK_SIZE);
	FILEIO_ResetFileSize(gl_BLog->f, CHANGE_LOG_BLOCK_SIZE);

	gl_FLog->m = gl_BLog->m = INVALID_FILE_HANDLE;
	gl_FLog->ptr = gl_BLog->ptr = NULL;
	gl_FLog->mapsize = gl_BLog->mapsize = 0;

	return 0;
}
static int checkLogState() {
	/*
	1. first boot, all empty , time tick all zieo.
	2. first boot, all empty , time tick B is bigger than F.
	3. F is not empty and time tick is zieo, B is empty and time tick is bigger than F.
	4. F is full and swap, set newest time tick complete, B is empty and time tick is smaller than F. than F will be B, B will be F.
	5. F is empty and keep recv new value, B is Full and flushing to dist, time tick B is Bigger than F.
	6. F is not empty, B is flushing to disk, time tick B is Bigger than F.
	6.1. F is empty, B has flush to dist complete, empty too, now F and B all empty and time tick B is bigger than F. 
	7. F is not empty and B is empty, time tick B is Bigger than F.
	8. goto 4
	*/

	LogFileHead* headF = gl_FLog->ptr;
	logItem* itemF = headF+1;
	LogFileHead* headB = gl_BLog->ptr;
	logItem* itemB = headB + 1;
	if (itemF->size == 0 && itemB->size == 0 && 
		headF->time == 0 && headB->time == 0) {
		return 1;
	}
	else if (itemF->size == 0 && itemB->size == 0 &&
		headF->time > headB->time) {
		return 2;
	}
	else if (itemF->size > 0 && itemB->size == 0 &&
		headF->time > headB->time) {
		return 3;
	}
	else if (itemF->size == 0 && itemB->size > 0 &&
		headF->time > headB->time) {
		return 5;
	}
	else if (itemF->size > 0 && itemB->size > 0 &&
		headF->time > headB->time) {
		return 6;
	}
	else if (itemF->size > 0 && itemB->size == 0 &&
		headF->time > headB->time) {
		return 7;
	}

	return -1;
}
static int init() {
	int err = 0;

	err = initStorageEngine("C:\\tmp\\");	ASSERT(err, "init storage fail");	if (err) { return -1; }
#ifdef DEBUG_CHANGE_LOG
	//err = initDiskLog(); ASSERT(err, "init disk log fail"); if (err) { return -1; }
#endif // DEBUG_CHANGE_LOG

	printm("**** Chang Log [");printBytes(CHANGE_LOG_BLOCK_SIZE);printm(" * 2] ****\n");
	err = StorageInitFileHandle(CHANGE_LOG_FILE_ID_A);
	err = StorageInitFileHandle(CHANGE_LOG_FILE_ID_B);
	gl_FLog->f = StorageGetFileHandle(CHANGE_LOG_FILE_ID_A);
	gl_BLog->f = StorageGetFileHandle(CHANGE_LOG_FILE_ID_B);

	if (CHANGE_LOG_BLOCK_SIZE <= sizeof(LogFileHead) + sizeof(logItem)) {
		ASSERT(TRUE, "invalid log block size");
		return -1;
	}

	err = flushAllLogWhenBoot(); if (err) { return -3; }
	gl_FLog = gl_logs + 0;
	gl_BLog = gl_logs + 1;

	gl_FLog->logFileId = CHANGE_LOG_FILE_ID_A;
	gl_FLog->flag.state = LOG_STATE_EMPTY;
	gl_FLog->cache.c1 = getBuffer(CHANGE_LOG_CACHE_SIZE_MIN, BUFFER_POOL); if (!gl_FLog->cache.c1) { return -2; }

	gl_BLog->logFileId = CHANGE_LOG_FILE_ID_B;
	gl_BLog->flag.state = LOG_STATE_EMPTY;
	gl_BLog->cache.c1 = getBuffer(CHANGE_LOG_CACHE_SIZE_MIN, BUFFER_POOL); if (!gl_BLog->cache.c1) { return -2; }

	initMutex(&gl_commandQueuePushMutex);
	initMutex(&gl_commandQueuePopMutex);
	initMutex(&gl_mutex);
	initMutex(&gl_BLog->readMutex);
	initMutex(&gl_FLog->readMutex);
	return 0;
}
static void cleanCommandItem(LogCommandItem* item) {
	lockMutex(&gl_commandQueuePopMutex);
	BuffRelease(&item->data);
	item->size = 0;
	unlockMutex(&gl_commandQueuePopMutex);
}

int initChangeLog() {
	return init();
}
bool ChangeLogIsReadingBLog() {
	return gl_BLog->flag.isReading;
}
void ChangeLogSetBLogReadLock() {
	ASSERT(gl_BLog->flag.isReading, "bit is 1 before set\n");
	gl_BLog->flag.isReading = 1u;
}
void ChangeLogSetBLogReadUnLock() {
	ASSERT(!gl_BLog->flag.isReading, "bit is 0 before clear\n");
	gl_BLog->flag.isReading = 0u;
}
int ChangeLogFlushPrepare(uint64 fileId, uint64 writingSize) {
	return flushLogPrepare(fileId, writingSize, 1);
}
int ChangeLogFlush(uint64 fileId, logItemType type, uint64 seek, uint64 size, char* data) {
	return flushLog(fileId, type, seek, size, data); 
}
int ChangeLogInsert(uint64 fileId, logItemType type, uint64 seek, BufferListInfo* data, uint64 offset, uint64 size, int* resp, void* callback) {
	return pushCmd(fileId, type, seek, data, offset, size, resp, callback);
}
int ChangeLogPopCmd(LogCommandItem* outItem) {
	return popCmd(outItem);
}
bool ChangeLogNeedCheckpoint() {
	return needCheckPoint();
}
void ChangeLogCheckpointBegin() {
	return checkpoint();
}
void ChangeLogCheckpointDone() {
	return checkpointDone();
}
int ChangeLogUpdateBackgroundLogToStorage() {
	return updateBackgroundLogToStorage();
}
int ChangeLogTryReadFromLog(uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset) {
	return readLog(fileId, seek, size, data, offset);
}
bool ChangeLogIsDestIdInLog(uint64 fileId){
	return isDestIdInLog(fileId);
}
int ChangeLogFlushAllLog() {
	return flushAllLog();
}
void ChangeLogCleanCommandItem(LogCommandItem* item) {
	cleanCommandItem(item);
}
int ChangeLogReadFromBLog(uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset) {
	ASSERT(!gl_BLog->flag.isReading, "not set flag: isReading\n");//ZBH ???
	return readFromLog(gl_BLog, fileId, seek, size, data, offset);
}
int ChangeLogReadFromFLog(uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset) {
	return readFromLog(gl_FLog, fileId, seek, size, data, offset);
}
int ChangeLogSwap() {
	return swap();
}
volatile uint8* test_getFlag() {
	return &gl_BLog->flag;
}

void ChangeLog_test1() {
	int err = 0;
	err = initBufferManagserBySize(1, BUFFER_POOL_NORMAL, BYTE_1G);
	err = initStorageEngine("C:\\tmp\\");	if (err) { return; }
	err = init(); if (err) { return; }

	printm("init success\n");

	uint64 fileId = 10;
	char data[16] = "123456789abcdefg";
	uint64 seek = 0;
	uint64 size = sizeof(data);
	for (uint32 i = 0; i < U16_MAX;i++) {
		err = ChangeLogFlushPrepare(fileId, 1);	//prepare log list
		ASSERT(err, "prepare insert log fail\n");
		//err = ChangeLogInsert(fileId, seek, size, data);
		ASSERT(err < 0, "push log fail");
		if (err == 1) {
			//err = ChangeLogFlushBLog();
			ASSERT(err, "flush BLog fail");
		}

		if (needCheckPoint()) {
			checkpoint();
			i--;
			continue;
		}
		

		if (i == 200) {
			//Blog empty, try read FLog
			BufferListInfo* tmp = NULL;
			uint64 off = 0;
			err = ChangeLogTryReadFromLog(fileId, 0, size, &tmp, &off);
			ASSERT(err, "");
			printm("%.*s\n", size, tmp->buff + off);
			BuffRelease(&tmp);
			off = 0;

			err = ChangeLogTryReadFromLog(fileId, 0, size/2, &tmp, &off);
			ASSERT(err, "");
			printm("%.*s\n", size/2, tmp->buff + off);
			BuffRelease(&tmp);
			off = 0;

			err = ChangeLogTryReadFromLog(fileId, 0+2, size / 2, &tmp, &off);
			ASSERT(err, "");
			printm("%.*s\n", size / 2, tmp->buff + off);
			BuffRelease(&tmp);
			off = 0;

			err = ChangeLogTryReadFromLog(fileId, 0, size + 1, &tmp, &off);
			if (err == 0) {
				printm("%.*s\n", size / 2, tmp->buff + off);
				BuffRelease(&tmp);
				off = 0;
			}
			else if (err == 1) {
				//err = ChangeLogFlushBLog();
			}
		}

		seek += size;
	}
	printm("flush success\n");
	
	err = removeLogFile();
	printm("clean log file\n");

	seek = 0;
	for (uint32 i = 0; i < U16_MAX; i++, seek+= size) {
		BufferListInfo* load = NULL;
		uint64 offset = 0;
		err = StorageRead(fileId, seek, size, &load, &offset);
		ASSERT(err, "read fail");
		printm("%.*s\n",size, load->buff + offset);
		BuffRelease(&load);
	}
}
void ChangeLog_test2() {
	int err = 0;

	err = initBufferManagserBySize(1, BUFFER_POOL, BYTE_1G);
	err = init(); ASSERT(err, "init chang log fail");
	printm("init success\n");
	uint64 cnt = U16_MAX*1;
	uint64 fileId = 10;
	//char data[160] = "123456789abcdefg123456789abcdefg123456789abcdefg123456789abcdefg123456789abcdefg123456789abcdefg123456789abcdefg123456789abcdefg123456789abcdefg123456789abcdefg";
	uint64 size = 1024 * 1.5;
	BufferListInfo* buf = getBuffer(size, BUFFER_POOL); ASSERT(err, "init buf fail");	memset(buf->buff, '6', size);
	uint64 seek = 0;
	char* data = buf->buff;
	printm("test case need total disk size = ");
	printBytes(cnt * size);
	printm(" + LogSize(%llu cnt)\n", cnt);
	hide_cursor();
	uint64 maxTime = 0;
	uint64 totalTime = 0;
	gl_fileWriteCnt = gl_fileReadCnt = 0;
	for (uint64 i = 0; i < cnt; i++) {
		uint64 start = myCounterStart();
		if (i == 109) {
			printm("");
		}
		err = ChangeLogInsert(fileId, LOG_TYPE_WRITE, seek, buf, 0, size, NULL, NULL);	//thread run 
		ASSERT(err, "insert log fail");
		{
			LogCommandItem item = { 0 };
			err = popCmd(&item);
			ASSERT(!err, "pop log command null\n");
			err = ChangeLogFlushPrepare(fileId, size); ASSERT(err < 0, "insert log prepare fail");
			if (err == 1) {
				//err = ChangeLogUpdateBackgroundLogToStorage();	//thread run
				//ASSERT(err, "flush BLog fail");
				//checkpoint();	//thread run
				//checkpointDone();
			}
			else if (err == 2) {
				err = ChangeLogUpdateBackgroundLogToStorage();	ASSERT(err < 0, "update log to storage fail");
				checkpoint();	//thread run
				checkpointDone();
				err = ChangeLogFlushPrepare(fileId, size); ASSERT(err < 0 || err == 2, "insert log prepare fail after flush logB");
			}

			start = myCounterStart();
			ASSERT(!item.data, "null data pointer\n");
			err = ChangeLogFlush(item.fileId, LOG_TYPE_WRITE, item.seek, item.size, item.data->buff);	ASSERT(err < 0, "write log fail");

		}
	
		uint64 us = myCounterUs(start); totalTime += us;
		print_progress_bar(i * 100 / cnt);
		//printm(" [i=\t%llu\n", i);
		
		if (us > maxTime) {
			maxTime = us;
			//printm(" max[%llu us]", maxTime);
		}
		seek += size;
	}
	print_progress_bar(100);
	printm(" I/O cnt: %llu/%llu, total ", gl_fileWriteCnt,gl_fileReadCnt); printUsTick(totalTime);
	show_cursor();
	printm("\nflush success, remainBuffer[%llu]\n", BuffGetPoolSize(BUFFER_POOL) - BuffGetUsedSizeByPool(BUFFER_POOL));


	seek = 0;
	printm("check disk1 data....\n");
	hide_cursor();
	for (uint64 i = 0; i < cnt; i++, seek += size) {
		BufferListInfo* load = NULL;
		uint64 offset = 0;
		if (i == 109) {
			printm("");
		}

		err = ChangeLogTryReadFromLog(fileId, seek, size, &load, &offset);
		if (err) {
			if (err == 1) {
				err = ChangeLogUpdateBackgroundLogToStorage();	ASSERT(err < 0, "update log to storage fail");
				checkpoint();	//thread run
				checkpointDone();
			}
			err = StorageRead(fileId, seek, size, &load, &offset);
		}
		ASSERT(err, "read fail");
		ASSERT(strncmp(load->buff + offset, data, size) != 0, "disk data error, i=%llu", i);
		//printm("%.*s\n", size, load->buff + offset);
		BuffRelease(&load);
		print_progress_bar(i * 100 / cnt);
	}
	print_progress_bar(100);
	show_cursor();
	err = flushAllLog(); ASSERT(err, "flush all log fail");
	seek = 0;
	printm("\ncheck disk2 data after flush all log....\n");
	hide_cursor();
	for (uint64 i = 0; i < cnt; i++, seek += size) {
		BufferListInfo* load = NULL;
		uint64 offset = 0;
		err = StorageRead(fileId, seek, size, &load, &offset);
		ASSERT(err, "read fail");
		ASSERT(strncmp(load->buff + offset, data, size) != 0, "disk data error");
		//printm("%.*s\n", size, load->buff + offset);
		BuffRelease(&load);
		print_progress_bar(i * 100 / cnt);
	}
	print_progress_bar(100);
	show_cursor();
	//err = flushAllLog();
	printm("\n**** pass ****\n");
	return;
}
void ChangeLog_test3() {
	/*
	300mb

	226 us
	278 us
	302 us
	307 us
	579 us
	602 us
	984 us
	8221 us
	14509 us
	23471 us
	more than 1000 81 cnt
	---------------------
	357 us
	33826 us		//??????
	more than 1000 1 cnt
	*/
	FILE_HANDLE f = INVALID_FILE_HANDLE;
	FILE_HANDLE m = INVALID_FILE_HANDLE;
	char* memPtr = NULL;
	uint64 mapSize = BYTE_1M*300;
	int err = 0;
	char* addr = "C:\\tmp\\!";
	err = FILEIO_Open(addr, &f);
	err = FILEIO_MemoryMap(f, &m, mapSize, &memPtr);

	uint64 seek = 0;
	char* ptr = memPtr;
	uint64 size = 512*2;
	uint64 maxTime = 0;
	uint64 moreThan100Cnt = 0;

	for (uint64 i = 0; ptr + size - memPtr < mapSize; i++, ptr += size) {
		uint64 start = myCounterStart();
		memset(ptr,3, size);
		FILEIO_MemoryFlush(ptr, size);
		uint64 us = myCounterUs(start);
		if (us > maxTime) {
			maxTime = us;
			printm("%llu us\n", maxTime);
		}
		if (us > 1000) {
			printm(""); moreThan100Cnt++;
		}
	}
	printm("more than 1000 %llu cnt\n", moreThan100Cnt);
	FILEIO_MemoryMapClose(m, memPtr, mapSize);
	moreThan100Cnt = 0;
	char buff[1024] = { 2 };
	seek = 0;
	maxTime = 0;
	printm("---------------------\n");
	err = FILEIO_MoveSeek(f, 0, SEEK_SET); ASSERT(err, "seek fail");
	for (uint64 i = 0; seek<mapSize; i++, seek += size) {
		
		memset(buff, 2, size);
		err = FILEIO_MoveSeek(f, seek, SEEK_SET); ASSERT(err, "seek fail");
		uint64 start = myCounterStart();
		err = FILEIO_Write(f, buff, size);
		//err = FILEIO_MoveSeek(f, size, SEEK_CUR); ASSERT(err, "seek fail");
		uint64 us = myCounterUs(start);
		ASSERT(err, "write fail");
		if (us > maxTime) {
			maxTime = us;
			printm("%llu us\n", maxTime);
		}
		if (us > 10000) {
			printm(""); moreThan100Cnt++;
		}
		memset(buff, 0, sizeof(buff));
		
	}
	printm("more than 1000 %llu cnt\n", moreThan100Cnt);
	return;
}
void ChangeLog_test4() {
	//command q
	int err = 0;

	err = initBufferManagserBySize(1, BUFFER_POOL, BYTE_1G);
	err = init(); ASSERT(err, "init chang log fail");
	printm("init success\n");
	uint32 cnt = 0;
	while (1) {
		err = pushCmd(0, LOG_TYPE_WRITE, 0, NULL, 0, 6, NULL, NULL);
		ASSERT(cnt< CHANGE_LOG_COMMAND_QUEUE_LEN_MAX && err, "push fail when queue not full\n");
		ASSERT(cnt >= CHANGE_LOG_COMMAND_QUEUE_LEN_MAX && !err, "push success when queue full\n");
		cnt++;
		if (cnt > CHANGE_LOG_COMMAND_QUEUE_LEN_MAX * 10) {
			break;
		}
	}
	printm("*** pass ***");
}