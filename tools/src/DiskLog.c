#include "MyTypes.h"
#include "MyTool.h"
#include <string.h>
#include <stdio.h>
#include "FileIO.h"
#include "ThreadManager.h"
#include "DiskLog.h"
#include "MyTime.h"

#ifdef DEBUG_DISK_LOG
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_DISK_LOG

typedef union logStringTag_t {
	uint8 dummy;
	struct {
		uint8 isValid : 1;

	};

}logStringTag;
typedef struct logString_t {
	logStringTag tag;
	char str[DISK_LOG_BUFFER_LEN_MAX];
}logString;
typedef struct context_t {
	logString logBuffer[DISK_LOG_QUEUE_LEN_MAX];
	uint32 headIndex;
	uint32 tailIndex;
	Mutex pushMutext;
	ThreadPoolSlot* threadSlot;

	FILE_HANDLE f;
	FILE_HANDLE m;
	char* ptr;
	char* cur;
	uint64 mapSize;
}context;

static context gl_context = { 0 };
static bool inited = FALSE;

static int extendFileSize(context* con, uint64 destSize) {
	int err = 0;
	if (con->mapSize == 0) {
		err = FILEIO_MemoryMap(con->f, &con->m, destSize, &con->ptr);
		if (!err) {
			con->cur = con->ptr;
			con->mapSize = destSize;
			memset(con->ptr, 0, con->mapSize);
		}
	}
	else {
		uint64 seek = con->cur - con->ptr;
		FILEIO_MemoryFlush(con->ptr, seek);
		err = FILEIO_MemoryReMap(con->f, &con->m, con->mapSize, destSize, &con->ptr);
		if (!err) {
			con->cur = con->ptr + seek;
			con->mapSize = destSize;
		}
	}
	
	return err;
}
static void thLoop(void* in, void* out) {
	int err = 0;
	context* con = in;
	printm("disk log server start...\n");
	while (1) {
		if (gl_context.logBuffer[gl_context.headIndex].tag.isValid) {
			uint64 len = MIN(strlen(gl_context.logBuffer[gl_context.headIndex].str), DISK_LOG_BUFFER_LEN_MAX);
			if (con->cur - con->ptr + len >= con->mapSize || con->mapSize == 0) {
				err = extendFileSize(con, con->mapSize + DISK_LOG_FILE_BLOCK_SIZE);
				if (err) {
					ASSERT(TRUE, "extend disk log file size fail\n");
					break;
				}
			}
			if (len == DISK_LOG_BUFFER_LEN_MAX) {
				gl_context.logBuffer[gl_context.headIndex].str[len - 1] = '\n';
			}
			memcpy(con->cur, gl_context.logBuffer[gl_context.headIndex].str, len);	con->cur += len;
			memset(gl_context.logBuffer[gl_context.headIndex].str, 0, DISK_LOG_BUFFER_LEN_MAX);
			gl_context.logBuffer[gl_context.headIndex].tag.isValid = FALSE;
			if (gl_context.headIndex + 1 >= DISK_LOG_QUEUE_LEN_MAX) {
				gl_context.headIndex = 0;
			}
			else {
				gl_context.headIndex++;
			}
			
		}

	}
	printm("disk log server stop...\n");
}
static void waitDone() {
	while (gl_context.headIndex != gl_context.tailIndex || gl_context.logBuffer[gl_context.headIndex].tag.isValid) {
		
	}
}
static int init() {
	int err = 0;
	if (inited) { return 0; }
	err = FILEIO_Init(); if (err) { return -1; }
	err = initThreadManager(); if (err) { return -2; }
	memset(gl_context.logBuffer, 0, DISK_LOG_QUEUE_LEN_MAX * DISK_LOG_BUFFER_LEN_MAX);
	gl_context.headIndex = gl_context.tailIndex = 0;
	char addr[256] = { 0 };
	memcpy(addr, DISK_LOG_PATH, strlen(DISK_LOG_PATH));
	char day[16] = { 0 };
	err = tickToYMD2(getCurTick(), day, sizeof(day)); if (err) { return -1; }
	memcpy(addr + strlen(addr), day, strlen(day));
	memcpy(addr + strlen(addr), ".log", 4);
	err = FILEIO_Open(addr, &gl_context.f); if (err) { return -1; }

	initMutex(&gl_context.pushMutext);
	ThreadParam param = {
	.input = &gl_context,
	.output = NULL,
	.func = thLoop
	};
	gl_context.threadSlot = ThreadRun(param);
	inited = TRUE;
	return 0;
}

void printLog(const char* format, ...) {
	va_list args;
	va_start(args, format);
	//vprintf(format, args);//debug
	lockMutex(&gl_context.pushMutext);
	time_t start = getCurTick();
	while (gl_context.logBuffer[gl_context.tailIndex].tag.isValid) {
		//printm("");
		if (getCurTick() - start > 60) {
			printm("push disk log timeout 60s\n");
			ASSERT(TRUE, "");
			exit(0);
		}
	}
	//memset(gl_context.logBuffer[gl_context.tailIndex].str, 0, DISK_LOG_BUFFER_LEN_MAX);
	vsnprintf(gl_context.logBuffer[gl_context.tailIndex].str, DISK_LOG_BUFFER_LEN_MAX+1, format, args);	//输出到指定位置
	gl_context.logBuffer[gl_context.tailIndex].tag.isValid = TRUE;
	gl_context.tailIndex++;
	if (gl_context.tailIndex >= DISK_LOG_QUEUE_LEN_MAX) {
		gl_context.tailIndex = 0;
	}
	unlockMutex(&gl_context.pushMutext);
	va_end(args);
}

int initDiskLog() {
	return init();
}
void DiskLogWaitAllDone() {
	waitDone();
}

void test_disklog() {

	int err = 0;
	err = init(); ASSERT(err, "init fail");

	for (uint32 i = 0; i < 1024;i++) {
		if (i == 37) {
			printm("");
		}
		printLog("1234567890zbcdefghijklmnopqrstuvwxyzAZCDEFGHIJKLMNOPQRSTUVWXYZ [%u]\n", i);
	}
	waitDone();
	printm("pass");
	return;
}