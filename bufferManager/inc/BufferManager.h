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
	StaticListIndexType listType;//�����б�
	BufferPoolBitMap Bitmap;
	Mutex mutex;
}BufferPool;

typedef struct BufferListInfo_t {
	uint64 size;
	char* buff;
	uint32 poolId;
	uint32 userNum;//��ģ��λ��λ��ÿ��taskģ��ֻ�ܻ�ȡһ��bufferlist������ȡ����ҪʹuserNum++��
	struct BufferListInfo_t* next;//������������ʽ����
	struct BufferListInfo_t* last;//������������ʽ����
}BufferListInfo;


int initBufferManagserBySize(uint32 poolCnt, ...);

bool isInvalidBufferList(BufferListInfo* bufferList);
int BuffRealloc(BufferListInfo** lastBuff, uint64 destSize, BufferPoolId poolId);//�ӳ��������������buffer���ɱ��pool
int BuffRequest(uint64 size, BufferPoolId poolId, BufferListInfo** out);//�����µ��ڴ��	//��Ҫ�Ż���ʱ����
void BuffRelease(BufferListInfo** buffList);//�ͷŶ�Ӧ�ڴ��
void DestroyBuffer();//�����ڴ�أ��ͷ���Դ
void BuffASyncIOIsGoingOn(BufferPool* pool);//����ִ�а����Ը�ģ���ڴ�η��ʵĶ����߳�ʱ����Ҫ���ã��Է�ֹ���ڴ������µ��ڴ���
void BuffASyncIODone(BufferPool* pool);//�����Ը�ģ���ڴ�η��ʵĶ����߳̽�������Ҫ���ã��Իָ��ڴ�������
uint64 BuffGetUsedSizeByPool(BufferPoolId poolId);
uint64 BuffGetPoolSize(BufferPoolId poolId);
int BuffGC(BufferPoolId poolId);
uint32 BuffGetRemainBufferListCnt();
void DEBUG_PrintBuffer();

void DEBUG_TestCase();

BufferListInfo* getBuffer(uint64 size, BufferPoolId poolId);
void freeBuffer(BufferListInfo** buffList);