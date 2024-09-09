#include "storageEngine.h"
#include "MyTool.h"
#include "DiskLog.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_STORAGE_ENGINE
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_STORAGE_ENGINE

#define BUFFER_POOL STORAGE_BUFFER_POOL
#define COMMAND_QUEUE_MAX_SIZE STORAGE_COMMAND_QUEUE_MAX_SIZE
typedef enum CommandType_e {
	STORAGE_INVALID,
	STORAGE_WRITE,
	STORAGE_READ,
	STORAGE_REMOVE,
}CommandType;
typedef struct CommandQueue_t {
	BufferListInfo* q;
	BufferListInfo* head;
	BufferListInfo* tail;
	uint32 cnt;
}CommandQueue;
typedef struct CommandInfo_t {
	uint64 fileId;
	CommandType type;
	uint32 diskBlockCnt;
	uint64 firstDiskBlockNum;

	BufferListInfo* seekList;
	BufferListInfo* dataList;
	BufferListInfo* sizeList;
}CommandInfo;
typedef struct flushItem_t {
	uint64 seek;
	BufferListInfo* data;
}flushItem;
typedef struct fileItem_t {
	FILE_HANDLE f;
	uint64 size;
}FileItem;

static char gl_mainPath[128] = {0};
static uint64 gl_diskBlockSize = 0;

static CommandInfo gl_commandQueue[COMMAND_QUEUE_MAX_SIZE] = { 0 };
static uint32 gl_cmdQueueHead = 0;
static uint32 gl_cmdQueueTail = 0;
static FILE_HANDLE gl_fdList[2048] = { INVALID_FILE_HANDLE };
static Mutex gl_Mutex[2048];
static Mutex gl_QueueMutex;
/*
1. file id -> path string
*/

int initStorageEngine(char* mainPath) {
	if (mainPath)
		memcpy(gl_mainPath, mainPath, strlen(mainPath));

	FILEIO_Init();
	gl_diskBlockSize = FILEIO_GetBlockSize();
	initMutex(&gl_QueueMutex);
	return 0;
}
int initFileHandle(uint64 fileId) {
	int err = 0;
	if (gl_fdList[fileId] == INVALID_FILE_HANDLE) {
		char addr[FILE_ADDR_MAX_LEN] = { 0 };
		err = fileIdToPath(fileId, addr);	if (err < 0) { return -1; }
		err = FILEIO_Open(addr, &(gl_fdList[fileId]));	if (err < 0) { return -1; }
		initMutex(&(gl_Mutex[fileId]));
	}
	return 0;
}
static int fileIdToPath(uint64 fileId, char outPath[FILE_ADDR_MAX_LEN]) {
	memset(outPath, 0, sizeof(outPath));
	uint64 len = strlen(gl_mainPath);
	memcpy(outPath, gl_mainPath, len);
	return base10ToBase59(fileId, outPath+len, &len);
}
static bool isFileExist(uint64 fileId) {
	int err = 0;
	char addr[FILE_ADDR_MAX_LEN] = { 0 };
	err = fileIdToPath(fileId, addr);
	return FILEIO_Exist(addr);
}
static int removeFile(uint64 fileId) {
	int err = 0;
	char addr[FILE_ADDR_MAX_LEN] = { 0 };
	err = fileIdToPath(fileId, addr);
	FILE_HANDLE f = gl_fdList[fileId];
	err = FILEIO_CloseFile(f); if (err) { 
		ASSERT(TRUE, "close file fail");
		return -1; 
	}
	gl_fdList[fileId] = INVALID_FILE_HANDLE;
	return FILEIO_Remove(addr);
}
static uint64 getFileSize(uint64 fileId) {
	int err = 0;
	char addr[FILE_ADDR_MAX_LEN] = { 0 };
	err = fileIdToPath(fileId, addr);
	FILE_HANDLE f = gl_fdList[fileId];
	if (f == NULL) {
		if (!FILEIO_Exist(addr)) { return 0; }
		else {
			//init file handle
			err = initFileHandle(fileId);
			if (err < 0) {
				ASSERT(TRUE, "init file handle file when open file in get file size function\n");
				return U64_MAX;
			}
			f = gl_fdList[fileId];
		}
	}

	return FILEIO_GetFileSize(f);
}

static int getMinAndMaxSeek(uint64* seekList, uint32 cnt, uint64* minIndex, uint64* maxIndex) {
	uint32 minSeekIndex = 0;
	uint32 maxSeekIndex = 0;
	for (uint32 i = 0; i < cnt;i++) {
		if (seekList[minSeekIndex] > seekList[i]) {
			minSeekIndex = i;
		}
		if (seekList[maxSeekIndex] < seekList[i]) {
			maxSeekIndex = i;
		}
	}
	*minIndex = minSeekIndex;
	*maxIndex = maxSeekIndex;
	return 0;
}
static CommandInfo makeCommandInfo(uint64 fileId, uint32 cnt, BufferListInfo* seekList, BufferListInfo* dataList, BufferListInfo* sizeList) {
	/*
	1.find start block list
	2.seek change
	*/
	int err = 0;
	uint64 firstBlockNum = 0;
	uint64 blockCnt = 0;
	CommandInfo info = { 0 };
	if (seekList && dataList && sizeList) {
		uint64 minSeekIndex = 0;
		uint64 maxSeekIndex = 0;
		err = getMinAndMaxSeek(seekList->buff, cnt, &minSeekIndex, &maxSeekIndex);
		uint64* sizeListPtr = sizeList->buff;
		uint64* seekListPtr = seekList->buff;

		firstBlockNum = seekListPtr[minSeekIndex] / gl_diskBlockSize;
		uint64 seekInBuffer = seekListPtr[minSeekIndex] % gl_diskBlockSize;
		uint64 endBlockNum = CEIL_DIVIDE(seekListPtr[maxSeekIndex] + sizeListPtr[maxSeekIndex], gl_diskBlockSize);	if (endBlockNum == 0) { endBlockNum = 1; }
		blockCnt = endBlockNum - firstBlockNum + 1;

		uint64 seekDiff = seekListPtr[minSeekIndex] - seekInBuffer;
		for (uint32 i = 0; i < cnt; i++) {
			seekListPtr[i] -= seekDiff;
		}
		info.type = STORAGE_WRITE;
	}
	else {
		info.type = STORAGE_REMOVE;
	}
	
	info.fileId = fileId;
	info.diskBlockCnt = blockCnt;
	info.firstDiskBlockNum = firstBlockNum;
	info.seekList = seekList;
	info.dataList = dataList;
	info.sizeList = sizeList;
	
	return info;
}
static int pushCommand(uint64 fileId, uint32 cnt, BufferListInfo* seekList, BufferListInfo* dataList, BufferListInfo* sizeList) {
	lockMutex(&gl_QueueMutex);
	if (gl_commandQueue[gl_cmdQueueTail].type != STORAGE_INVALID) {
		unlockMutex(&gl_QueueMutex);
		return -2;
	}
	int err = 0;
	if (gl_fdList[fileId] == INVALID_FILE_HANDLE) {
		err = initFileHandle(fileId);	if (err < 0) { 
			unlockMutex(&gl_QueueMutex);
			return -1; }
	}
	if (cnt == 0) {
		unlockMutex(&gl_QueueMutex);
		return 0;
	}
	
	gl_commandQueue[gl_cmdQueueTail] = makeCommandInfo(fileId, cnt, seekList, dataList, sizeList);
	gl_cmdQueueTail++;	if (gl_cmdQueueTail >= COMMAND_QUEUE_MAX_SIZE) { gl_cmdQueueTail = 0; }

	unlockMutex(&gl_QueueMutex);
	return 0;
}
static CommandInfo* popCommand() {
	lockMutex(&gl_QueueMutex);
	CommandInfo* ret = gl_commandQueue + gl_cmdQueueHead;
	if (ret->type == STORAGE_INVALID) {
		unlockMutex(&gl_QueueMutex);
		return NULL;
	}
	gl_cmdQueueHead++;	if (gl_cmdQueueHead >= COMMAND_QUEUE_MAX_SIZE) { gl_cmdQueueHead = 0; }
	unlockMutex(&gl_QueueMutex);
	return ret;
}
static void showStorageCommandQueue() {
	if ((gl_commandQueue + gl_cmdQueueHead)->seekList == NULL) {
		printm("***null command queue***\n");
		return;
	}
	for (uint32 i = gl_cmdQueueHead;;) {
		CommandInfo* info = gl_commandQueue+i;
		if (info->seekList == NULL) { break; }

		printm("id[%llu] block0[%llu] cnt[%llu]\n", info->fileId, info->firstDiskBlockNum, info->diskBlockCnt);
		for (uint32 j = 0; j < info->seekList->size / sizeof(uint64);j++) {
			uint64* seek = info->seekList->buff;
			uint64* size = info->sizeList->buff;
			char* data = info->dataList->buff;
			printm("[%llu:%.*s]", seek[j], size[j], data);
			data += *size;
		}
		printm("\n");
		i++;	if (i >= COMMAND_QUEUE_MAX_SIZE) { i = 0; }
	}
}
static int flushStorageCommand(CommandInfo* cmd) {
	int err = 0;
	BufferListInfo* tmp = NULL;
	if (gl_fdList[cmd->fileId] == INVALID_FILE_HANDLE) {
		err = initFileHandle(cmd->fileId);	if (err < 0) { return -1; }
	}
	FILE_HANDLE f = gl_fdList[cmd->fileId];
	
	if (cmd->type == STORAGE_WRITE) {
		//write
		uint64 allSize = cmd->diskBlockCnt * gl_diskBlockSize;
		uint64 fileSeek = cmd->firstDiskBlockNum * gl_diskBlockSize;
		uint32 dataCnt = cmd->seekList->size / sizeof(uint64);
		tmp = getBuffer(allSize, BUFFER_POOL);

		ASSERT(f == NULL, "file handle have not inited.");
		uint64 fileSize = FILEIO_GetFileSize(f);
		Mutex* mutex = gl_Mutex + cmd->fileId;

		uint32 loadSize = 0;
		if (fileSize < fileSeek) {
			loadSize = 0;
		}
		else {
			loadSize = MIN(allSize, fileSize - fileSeek);
		}
		if (loadSize > 0) {
			lockMutex(mutex);
			err = FILEIO_MoveSeek(f, fileSeek, SEEK_SET);	ASSERT(err, "move seek fail.");
			err = FILEIO_Read(f, tmp->buff, loadSize);
			unlockMutex(mutex);
			if (err) { ERROR(-3); }
		}
		char* data = cmd->dataList->buff;
		uint64* seek = cmd->seekList->buff;
		uint64* size = cmd->sizeList->buff;

		for (uint32 i = 0; i < dataCnt; i++, data += *size, seek++, size++) {
			//printm("write seek[%llu] size[%llu]\n", *seek + fileSeek, *size);
			memcpy(tmp->buff + *seek, data, *size);
		}
		lockMutex(mutex);
		err = FILEIO_MoveSeek(f, fileSeek, SEEK_SET);	if (err) {
			unlockMutex(mutex);
			ERROR(-4);
		}
		err = FILEIO_Write(f, tmp->buff, tmp->size);	if (err) {
			unlockMutex(mutex);
			ERROR(-5);
		}
		unlockMutex(mutex);

		BuffRelease(&cmd->seekList);	BuffRelease(&cmd->dataList);	BuffRelease(&cmd->sizeList);
		memset(cmd, 0, sizeof(CommandInfo));
		BuffRelease(&tmp);
	}
	else if (cmd->type == STORAGE_REMOVE){
		//remove
		err = StorageRemoveFile(cmd->fileId); if (err) { return err; }
	}
	else {
		ASSERT(TRUE, "invalid storage command type\n");
		return -1;
	}
	
	return 0;
error:
	BuffRelease(&tmp);
	return err;
}

static int read(uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset) {
	int err = 0;
	if (gl_fdList[fileId] == INVALID_FILE_HANDLE) {
		err = initFileHandle(fileId);	if (err < 0) { return -1; }
	}
	Mutex* mutex = gl_Mutex + fileId;
	FILE_HANDLE f = gl_fdList[fileId];
	uint32 firstBlockNum = seek / gl_diskBlockSize;
	uint32 blockCnt = CEIL_DIVIDE(seek + size - (firstBlockNum* gl_diskBlockSize), gl_diskBlockSize);
	*data = getBuffer(blockCnt*gl_diskBlockSize, BUFFER_POOL);
	if (!(*data)) { return -2; }
	lockMutex(mutex);
	err = FILEIO_MoveSeek(f, (firstBlockNum)*gl_diskBlockSize, SEEK_SET);	if (err < 0) { 
		unlockMutex(mutex);
		ERROR(-2); }
	err = FILEIO_Read(f, (*data)->buff, (*data)->size);	if (err < 0) { 
		unlockMutex(mutex);
		ERROR(-2); }
	unlockMutex(mutex);
	*offset = seek % gl_diskBlockSize;

	return 0;

error:
	BuffRelease(data);
	return err;
}
static FILE_HANDLE getFileHandle(uint64 fileId) {
	return gl_fdList[fileId];
}

int StorageGetFilePathById(uint64 fileId, char outPath[FILE_ADDR_MAX_LEN]) {
	return fileIdToPath(fileId, outPath);
}
int StoragePushCommand(uint64 fileId, uint32 cnt, BufferListInfo* seekList, BufferListInfo* dataList, BufferListInfo* sizeList) {
	return pushCommand(fileId, cnt, seekList, dataList, sizeList);
}
void StorageDoCommand() {
	int err = 0;
	while (1) {
		CommandInfo* cmd = popCommand();
		if (!cmd) { break; }
		err = flushStorageCommand(cmd);
		ASSERT(err, "flush storage command fail!! [%d]\n", err);
	}
	
}

int StorageRead(uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset) {
	return read(fileId, seek, size, data, offset);
}
bool StorageIsFileExist(uint64 fileId) {
	return isFileExist(fileId);
}
uint64 StorageGetFileSize(uint64 fileId) {
	return getFileSize(fileId);
}
int StorageRemoveFile(uint64 fileId) {
	return removeFile(fileId);
}
FILE_HANDLE StorageGetFileHandle(uint64 fileId) {
	return getFileHandle(fileId);
}
int StorageInitFileHandle(uint64 fileId) {
	return initFileHandle(fileId);
}

void Storage_test1() {
	int err = 0;
	uint32 cnt = 3;

	err = initStorageEngine("C:\\tmp\\");
	uint32 fileId = 0;
	char data[512] = { 0 };
	uint64 destSeek[3] = { 510, 530, 1020 };
	fileIdToPath(fileId, data);
	BufferListInfo* seekList = getBuffer(cnt * sizeof(uint64), BUFFER_POOL_NORMAL);
	uint64* seek = seekList->buff;	seek[0] = destSeek[0]; seek[1] = destSeek[1]; seek[2] = destSeek[2];
	/*
	seek[0] = 0; seek[1] = 530; seek[2] = 1024;
	seek[0] = 510; seek[1] = 530; seek[2] = 1024;
	seek[0] = 512; seek[1] = 530; seek[2] = 1024;
	*/
	BufferListInfo* dataList = getBuffer(strlen(data)*3, BUFFER_POOL_NORMAL);
	BufferListInfo* sizeList = getBuffer(sizeof(uint64) * 3, BUFFER_POOL_NORMAL);
	char* dataPtr = dataList->buff;
	uint64* size = sizeList->buff;
	memcpy(dataPtr, data, strlen(data)); dataPtr += strlen(data);
	memcpy(dataPtr, data, strlen(data)); dataPtr += strlen(data);
	memcpy(dataPtr, data, strlen(data)); 
	size[0] = size[1] = size[2] = strlen(data);

	err = pushCommand(fileId, 3, seekList, dataList, sizeList);
	ASSERT(err, "push command fail");

	showStorageCommandQueue();
	CommandInfo* cur = popCommand();
	ASSERT(cur == NULL, "pop command fail");
	err = flushStorageCommand(cur);	if (err) { printm("flush file fail."); }
	ASSERT(err, "flush command fail");

	BufferListInfo* load = NULL;
	uint64 offset = 0;

	err = read(fileId, destSeek[0], size[0], &load, &offset); if (err) { ASSERT(TRUE, "read fail"); }
	printm("%.*s\n", size[0], load->buff+offset);
	BuffRelease(&load);

	err = read(fileId, destSeek[1], size[1], &load, &offset); if (err) { ASSERT(TRUE, "read fail"); }
	printm("%.*s\n", size[1], load->buff + offset);
	BuffRelease(&load);

	err = read(fileId, destSeek[2], size[2], &load, &offset); if (err) { ASSERT(TRUE, "read fail"); }
	printm("%.*s\n", size[2], load->buff + offset);
	BuffRelease(&load);


	return;

	char loadData[2048] = { 0 };
	FILE_HANDLE f = gl_fdList[fileId];
	uint64 fileSize = FILEIO_GetFileSize(f);
	err = FILEIO_MoveSeek(f, 0, SEEK_SET);
	err = FILEIO_Read(gl_fdList[fileId], loadData, fileSize);
	
	for (uint32 i = 0; i < 3;i++) {
		printm("%.*s\n", strlen(data), loadData + destSeek[i]);
	}

	FILEIO_CloseFile(f);

}
void Storage_test2() {
	int err = 0;
	uint64 fileId = 0;
	err = initStorageEngine("c:\\tmp\\"); if (err) { printm("init fail\n"); return; }

	ASSERT(isFileExist(fileId), "1");
	
	err = pushCommand(fileId, 0, NULL, NULL, NULL); ASSERT(err, "2");
	StorageDoCommand();

	ASSERT(!isFileExist(fileId), "3");

	uint64 size = getFileSize(fileId);
	ASSERT(size != 0, "4");

	err = removeFile(fileId); ASSERT(err, "5");
	ASSERT(isFileExist(fileId), "6");
	//create same name file after remove, but check exist fail!!!
	BufferListInfo* seekList = getBuffer(sizeof(uint64), BUFFER_POOL);
	BufferListInfo* dataList = getBuffer(sizeof(uint64), BUFFER_POOL);
	BufferListInfo* sizeList = getBuffer(sizeof(uint64), BUFFER_POOL);
	uint64* seek = seekList->buff; *seek = 0;
	uint64* data = dataList->buff; *data = 1;
	uint64* size1 = sizeList->buff; *size1 = sizeof(uint64);
	err = pushCommand(fileId, 0, NULL, NULL, NULL); ASSERT(err, "7");
	StorageDoCommand();

	ASSERT(!isFileExist(fileId), "8");

	err = removeFile(fileId); ASSERT(err, "9");
	ASSERT(isFileExist(fileId), "10");

	printm("pass\n");
}
