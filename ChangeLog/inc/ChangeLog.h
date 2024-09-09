#pragma once
#include "MyTypes.h"
#include "BufferManager.h"
#include "storageEngine.h"

#define DEBUG_CHANGE_LOG

#define CHANGE_LOG_LOG_CNT_MAX_PER_FILE (STORAGE_COMMAND_QUEUE_MAX_SIZE)
#define CHANGE_LOG_FILE_ID_A (0)
#define CHANGE_LOG_FILE_ID_B (1)
#define CHANGE_LOG_BLOCK_SIZE (BYTE_1M)
#define CHANGE_LOG_FILE_ID_MAX (BYTE_1K*2)
#define CHANGE_LOG_LIST_BLOCK_CNT (256U)
#define CHANGE_LOG_THRESHOLD_SWAP_SIZE (BYTE_1M*32)
#define CHANGE_LOG_THRESHOLD_LOG_ITEM_CNT (CHANGE_LOG_LOG_CNT_MAX_PER_FILE*0.7)

#define CHANGE_LOG_COMMAND_QUEUE_LEN_MAX (32U)

#define CAHNGE_LOG_BUFFER_POOL BUFFER_POOL_NORMAL

typedef enum logItemType_e {
	LOG_TYPE_WRITE,
	LOG_TYPE_REMOVE,
}logItemType;

typedef struct LogCommandItem_t {
	uint64 fileId;
	uint64 seek;
	BufferListInfo* data;
	uint32 offset;
	uint32 size;
	int* resp;
	void* callback;
}LogCommandItem;

int initChangeLog();
bool ChangeLogIsReadingBLog();
void ChangeLogSetBLogReadLock();
void ChangeLogSetBLogReadUnLock();
int ChangeLogInsert(uint64 fileId, logItemType type, uint64 seek, BufferListInfo* data, uint64 offset, uint64 size, int* resp, void* callback);
int ChangeLogPopCmd(LogCommandItem* outItem);
int ChangeLogFlushPrepare(uint64 fileId, uint64 writingSize);
int ChangeLogFlush(uint64 fileId, logItemType type, uint64 seek, uint64 size, char* data);
bool ChangeLogNeedCheckpoint();//check is it time to flush BLog
void ChangeLogCheckpointBegin();//time-consuming task
void ChangeLogCheckpointDone();//finish check-point
int ChangeLogUpdateBackgroundLogToStorage();//push IO commands to storage engine from change log, you need trigger checkpoint to real flush these commands

int ChangeLogTryReadFromLog(uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset);//0: readed; 1:need flush BLog and try again; 2: not found; -2: need flush and BLog is flushing
bool ChangeLogIsDestIdInLog(uint64 fileId);
int ChangeLogFlushAllLog();
void ChangeLogCleanCommandItem(LogCommandItem* item);
int ChangeLogReadFromBLog(uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset);
int ChangeLogReadFromFLog(uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset);
int ChangeLogSwap();
volatile uint8* test_getFlag();


void ChangeLog_test1();
void ChangeLog_test2();
void ChangeLog_test3();