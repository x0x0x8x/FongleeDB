#include "MyTypes.h"
#include "ThreadManager.h"
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#ifdef _WIN32
// Windows 
#include <Windows.h>
#elif __linux__
// Linux 
#include <pthread.h>
#else
// other

#endif


#ifdef DEBUG_THREAD_POOL
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TASK_HANDLER

//线程池
ThreadPoolSlot gl_ThreadPool[THREAD_MAX] = { 0 };
ThreadPoolBimap gl_ThreadPoolBitmap = {0};
uint8 gl_err = 0;


void freeThread(ThreadPoolSlot* slot);

#ifdef WIN32	
void waitSemaphore(HANDLE semaphore);
#else
void waitSemaphore(pthread_cond_t* semaphore, Mutex* mutex);
#endif

// 线程函数

#ifdef _WIN32
// Windows 
DWORD WINAPI ThreadRunFunc(LPVOID param) {
#elif __linux__
// Linux 
void* ThreadRunFunc(void* param) {
#else
// other

#endif
	ThreadPoolSlot* slot = ((ThreadParam*)param)->remain;
	printm("Thread [%d] active\n", slot - gl_ThreadPool);
	//input: uint32 index
	gl_err++;
	if (gl_err == THREAD_MAX)
		gl_ThreadPoolBitmap.Inited = TRUE;
	while (TRUE) {
		// 在这里编写线程要执行的代码	
#ifdef WIN32
		waitSemaphore(slot->semaphore);
#else
		waitSemaphore(&(slot->semaphore), &(slot->mutex));
#endif // WIN32

		//printf("slot[%lld]\n", slot - gl_ThreadPool);

		if (slot->param.func == NULL)
		{
			printm("Thread error: param.func = NULL\n");
		}
		else {
			slot->param.func(slot->param.input, slot->param.output);
		}
		freeThread(slot);
	}

	//never hear;

#ifdef _WIN32
	// Windows
	return 0;
#elif __linux__
	// Linux
	return NULL;
#else
	// other
#endif
}

int initThreadPoolSlot(uint32 index) {
	initMutex(&(gl_ThreadPool[index].mutex));
	
	gl_ThreadPool[index].param.remain = &(gl_ThreadPool[index]);
#ifdef WIN32
	gl_ThreadPool[index].semaphore = CreateSemaphore(NULL, 0, 1, NULL);
	gl_ThreadPool[index].hThread = CreateThread(NULL, 0, ThreadRunFunc, &(gl_ThreadPool[index].param), 0, &(gl_ThreadPool[index].threadId));
	ASSERT(gl_ThreadPool[index].hThread == NULL, "init thread fail");
#else
	pthread_cond_init(&(gl_ThreadPool[index].semaphore), NULL);
	gl_ThreadPool[index].hThread = pthread_create(&(gl_ThreadPool[index].threadId), NULL, ThreadRunFunc, &(gl_ThreadPool[index].param));
	ASSERT(gl_ThreadPool[index].hThread != 0, "init thread fail");
#endif // WIN32

	return 0;
}

int initThreadManager() {
	if (gl_ThreadPoolBitmap.Inited) { return 0; }
	int err = 0;
	uint32 index = 0;
	memset(gl_ThreadPool, 0, sizeof(gl_ThreadPool));
	for (;index < THREAD_MAX;index++) {
		err = initThreadPoolSlot(index);
		ASSERT(err < 0, "initThreadManager fail");
	}
	while (!gl_ThreadPoolBitmap.Inited) {}
	return 0;
}

uint32 getAndLockFreeThread() {
	uint32 ret = 0;
	for (;ret < THREAD_MAX;ret++) {
		if (gl_ThreadPool[ret].Bitmap.lock)
			continue;
		else {
			gl_ThreadPool[ret].Bitmap.lock = TRUE;
			return ret;
		}
	}
	return INVALID_THREAD;
}

//#ifdef WIN32
//void resetSemphore(HANDLE semaphore) {
//	bool err = ResetEvent(semaphore);
//	ASSERT(!err, "reset semphore fail");
//}
//#else
//void resetSemphore(pthread_cond_t* semaphore, Mutex* mutex) {
//	bool err = 0;
//	pthread_mutex_lock(mutex);
//	pthread_cond_init(semaphore, NULL);
//	pthread_mutex_unlock(mutex);
//	//ASSERT(!err, "reset semphore fail");
//}
//#endif // WIN32

void freeThread(ThreadPoolSlot* slot) {
	//printm("thread free\n");
	unlockMutex(&(slot->mutex));
	memset(&slot->param, 0, sizeof(ThreadParam));
	slot->Bitmap.lock = FALSE;
}


#ifdef WIN32	
void waitSemaphore(HANDLE semaphore) {
	WaitForSingleObject(semaphore, INFINITE);
}
#else
void waitSemaphore(pthread_cond_t* semaphore, Mutex* mutex) {
	lockMutex(mutex);

	pthread_cond_wait(semaphore, mutex);

	unlockMutex(mutex);
}
#endif // WIN32

#ifdef WIN32
void sendSemphore(HANDLE semaphore) {
	ReleaseSemaphore(semaphore, 1, NULL);
}
#else
void sendSemphore(pthread_cond_t* semaphore) {
	pthread_cond_signal(semaphore);
}
#endif // WIN32



ThreadPoolSlot* ThreadRun(ThreadParam param) {
	uint32 index = getAndLockFreeThread();
	if (index == INVALID_THREAD) {
		ASSERT(index == INVALID_THREAD, "invalid slot index");
		return NULL;
	}
	
	ThreadPoolSlot* slot = &(gl_ThreadPool[index]);
	memcpy(&(slot->param), &param, sizeof(param));	
#ifdef WIN32
	sendSemphore(slot->semaphore);
#else
	sendSemphore(&slot->semaphore);
#endif // WIN32
	
	return slot;
}
