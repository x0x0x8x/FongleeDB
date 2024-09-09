#pragma once
#include "MyTypes.h"
#include "MyTool.h"

#define THREAD_POOL
#define DEBUG_THREAD_POOL

#define THREAD_MAX (CPU_CORE_NUM_MAX)

#define INVALID_THREAD U32_MAX

typedef struct ThreadParam_t {
	void* input;
	void* output;
	void (*func)(void*, void*);
	void* remain;
}ThreadParam;

typedef struct ThreadPoolSlotBitMap_t {
	uint8 lock : 1;
	uint8 Sleeping : 1;
	uint8 rem1 : 1;
	uint8 rem2 : 1;
	uint8 rem3 : 1;
	uint8 rem4 : 1;
	uint8 rem5 : 1;
	uint8 rem6 : 1;
}ThreadPoolSlotBitMap;

typedef struct ThreadPoolSlot_t {

	Mutex mutex;
	ThreadParam param;
#ifdef WIN32
	HANDLE hThread;
	DWORD threadId;
	HANDLE semaphore;
#else
	int hThread;
	pthread_t threadId;
	pthread_cond_t semaphore;
#endif // WIN32


	ThreadPoolSlotBitMap Bitmap;
}ThreadPoolSlot;

typedef struct ThreadPoolBimap_t {
	uint8 Inited : 1;
	uint8 tag2 : 1;
	uint8 tag3 : 1;
	uint8 tag4 : 1;
	uint8 tag5 : 1;
	uint8 tag6 : 1;
	uint8 tag7 : 1;
	uint8 tag8 : 1;
}ThreadPoolBimap;

int initThreadManager();
ThreadPoolSlot* ThreadRun(ThreadParam param);
