#pragma once
#include "MyTypes.h"

#define DEBUG_FILE_IO

#ifdef WIN32
#define INVALID_API_FILE_HANDLE (INVALID_HANDLE_VALUE)
#define INVALID_FILE_HANDLE (NULL)
#else
#define INVALID_API_FILE_HANDLE (-1)
#define INVALID_FILE_HANDLE (-1)

#define _fseeki64 fseeko
#define _ftelli64 ftello
#define HANDLE int
#endif // WIN32

#define FILE_ADDR_MAX_LEN (255U)
#define FILE_SIZE_MAX (U64_MAX - 1)
#ifdef WIN32
#define MAP_BLOCK_SIZE (BYTE_1K*64)
#else
#define MAP_BLOCK_SIZE (1)
#endif // WIN32



#ifdef WIN32
#define FILE_HANDLE HANDLE
#else
#define FILE_HANDLE int
#endif // WIN32

typedef enum FileOpenType_e {
#ifdef WIN32
	ONLY_READ = 1,
	ONLY_WRITE = 2,
	EXECUTE = 3,//可执行文件的执行权限
	ALL_PER = 4,
	SYNC_IO = 5,
#else
	ONLY_READ = 1,
	ONLY_WRITE = 2,
	EXECUTE = 3,//可执行文件的执行权限
	ALL_PER = 4,
	SYNC_IO = 5,
#endif // WIN32
}FileOpenType;

//一个文件的句柄
typedef struct FileHandler_t {
	char addr[FILE_ADDR_MAX_LEN];	//该文件的绝对地址
	FILE_HANDLE f;//file handler
#ifdef WIN32
	HANDLE m;//mapping handler
#else
#endif // WIN32
	uint64 mapSize;//
	char* ptr;
}FileHandler;

int FILEIO_Init();
bool FILEIO_Exist(char* addr);
int FILEIO_Open(char* fileAddr, FILE_HANDLE* hFile);
int FILEIO_MoveSeek(FILE_HANDLE hFile, int64 seek, int origin);
int FILEIO_CloseFile(FILE_HANDLE hFile);
int FILEIO_Read(FILE_HANDLE hFile, char* buff, uint64 len);
int FILEIO_Write(FILE_HANDLE hFile, char* buff, uint64 len);
uint64 FILEIO_GetBlockSize();
int FILEIO_Remove(char* addr);
int FILEIO_Rename(char* addr, char* dest);
uint64 FILEIO_GetFileSize(FILE_HANDLE hFile);
int FILEIO_CreateDirectory(char* path);
int FILEIO_ResetFileSize(FILE_HANDLE f, uint64 newSize);


int FILEIO_MemoryMap(FILE_HANDLE f, FILE_HANDLE* m, uint64 size, char** ptr);
void FILEIO_MemoryMapClose(FILE_HANDLE m, char* ptr, uint64 size);
void FILEIO_MemoryFlush(char* ptr, uint64 size);
int FILEIO_MemoryReMap(FILE_HANDLE f, FILE_HANDLE* m, uint64 curSize, uint64 size, char** ptr);

void testStorageHandler();
void testDiskIO();
void testFileMapIO();
void testFileWriteLatency();
void testFileRight();
void testFile_test2_diffFileHandle();