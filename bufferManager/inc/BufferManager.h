#pragma once
#include "MyTypes.h"
#include "StaticListManager.h"
#include <stdarg.h>

/*
* 
* 
* 
*/

#define BUFFER_MANAGER

//#define DEBUG_BUFFER_MANAGER

#define MAX_BUFF_POOL_CNT (10)
#define BUFFER_LIST_WEIGHT (10000)
#define BUFFER_LIST_MIN_SIZE (4096)

#define INVALID_BUFFER_POOL(pool) (pool < 0 || pool >= BUFFER_POOL_MAX)

typedef enum BufferPoolId_e {
	BUFFER_POOL_NORMAL,	
	BUFFER_POOL_MAX

}BufferPoolId;


typedef struct BufferManagerBitMap_t {
	uint8 Inited : 1;
	uint8 Sleeping : 1;
	uint8 rem1 : 1;
	uint8 rem2 : 1;
	uint8 rem3 : 1;
	uint8 rem4 : 1;
	uint8 rem5 : 1;
	uint8 rem6 : 1;
}BufferManagerBitMap;

typedef struct BufferPoolBitMap_t {
	uint8 lock : 1;
	uint8 isGC : 1;
	uint8 rem1 : 1;
	uint8 rem2 : 1;
	uint8 rem3 : 1;
	uint8 rem4 : 1;
	uint8 rem5 : 1;
	uint8 rem6 : 1;
}BufferPoolBitMap;

typedef struct BufferPool_t {
	uint64 size;
	uint64 remainSize;
	char* buffHead;
	uint32 weight;
	StaticListIndexType listType;//索引列表
	BufferPoolBitMap Bitmap;
	Mutex mutex;
}BufferPool;

typedef struct BufferListInfo_t {
	uint64 size;
	char* buff;
	uint32 poolId;
	uint32 userNum;//以模块位单位，每个task模块只能获取一次bufferlist，并获取后需要使userNum++；
	struct BufferListInfo_t* next;//用于以链表形式存在
	struct BufferListInfo_t* last;//用于以链表形式存在
}BufferListInfo;


int initBufferManagserBySize(uint32 poolCnt, ...);

bool isInvalidBufferList(BufferListInfo* bufferList);
int BuffRealloc(BufferListInfo** lastBuff, uint64 destSize, BufferPoolId poolId);//延长或缩短已申请的buffer，可变更pool
int BuffRequest(uint64 size, BufferPoolId poolId, BufferListInfo** out);//申请新的内存段	//需要优化锁时常！
void BuffRelease(BufferListInfo** buffList);//释放对应内存段
void DestroyBuffer();//销毁内存池，释放资源
void BuffASyncIOIsGoingOn(BufferPool* pool);//即将执行包含对该模块内存段访问的独立线程时，需要调用，以防止因内存整理导致的内存损坏
void BuffASyncIODone(BufferPool* pool);//包含对该模块内存段访问的独立线程结束后，需要调用，以恢复内存整理功能
uint64 BuffGetUsedSizeByPool(BufferPoolId poolId);
uint64 BuffGetPoolSize(BufferPoolId poolId);
int BuffGC(BufferPoolId poolId);
uint32 BuffGetRemainBufferListCnt();
void DEBUG_PrintBuffer();

void DEBUG_TestCase();

BufferListInfo* getBuffer(uint64 size, BufferPoolId poolId);
void freeBuffer(BufferListInfo** buffList);