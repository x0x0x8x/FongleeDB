#include "MyTypes.h"
#include "BufferManager.h"
#include "FileIO.h"

#define DEBUG_STORAGE_ENGINE

#define STORAGE_COMMAND_QUEUE_MAX_SIZE (64U)
#define STORAGE_BUFFER_POOL (BUFFER_POOL_NORMAL)

int initStorageEngine(char* mainPath);
int StorageGetFilePathById(uint64 fileId, char outPath[FILE_ADDR_MAX_LEN]);
int StoragePushCommand(uint64 fileId, uint32 cnt, BufferListInfo* seekList, BufferListInfo* dataList, BufferListInfo* sizeList);
void StorageDoCommand();

int StorageRead(uint64 fileId, uint64 seek, uint64 size, BufferListInfo** data, uint64* offset);
FILE_HANDLE StorageGetFileHandle(uint64 fileId);
int StorageInitFileHandle(uint64 fileId);

bool StorageIsFileExist(uint64 fileId);
uint64 StorageGetFileSize(uint64 fileId);
int StorageRemoveFile(uint64 fileId);

void Storage_test1();
void Storage_test2();