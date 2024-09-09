#include "FileIO.h"
#include "MyTool.h"
#include "MyTime.h"
#include <stdio.h>

#ifdef WIN32
#else
#define _GNU_SOURCE
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/types.h>
//#include <asm-generic/fcntl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>

#endif // WIN32


#define ERROR(errCode) {err = errCode;goto error;}

#ifdef DEBUG_FILE_IO
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TCP

#define TEST_ADDR "C:\\tmp\\file.txt"
#define TEST_ADDR2 "C:\\tmp\\file1.txt"

static uint64 gl_diskSectorSize = 0;
uint64 gl_fileReadCnt = 0;
uint64 gl_fileWriteCnt = 0;
static bool inited = FALSE;

int closeFile(FILE_HANDLE hFile);

#ifdef WIN32
int getDiskInfoWin(uint64* lpSectorsPerCluster, uint64* lpBytesPerSector, uint64* lpNumberOfFreeClusters, uint64* lpTotalNumberOfClusters) {
	int err = 0;
	err = GetDiskFreeSpace(NULL, lpSectorsPerCluster, lpBytesPerSector, lpNumberOfFreeClusters, lpTotalNumberOfClusters);
	if (err == 0) { err = -1; }
	else { err = 0; }

	return err;
}
#endif

uint64 getBytesPerSector() {
	int err = 0;
	uint64 ret = 0;
#ifdef WIN32
	err = getDiskInfoWin(NULL, &ret, NULL, NULL); if (err < 0) { return 0; }
#else
	struct statvfs fs_info;
	if (statvfs("/", &fs_info) != 0) {
		perror("statvfs error");
		return -1;
	}
	ret = fs_info.f_frsize; // 获取分配块的前端大小
#endif // WIN32

	return ret;
}

int initFileIO() {
	if (inited) { return 0; }
	printm("***** File IO *****\n");
	printm("Must run as administrator !!!\n");
#ifdef WIN32
#else
#ifndef O_DIRECT
	printm("can not direct disk io !!!\n");
#define O_DIRECT	00040000
	return -1;
#endif // !O_DIRECT
#endif // WIN32

	gl_diskSectorSize = getBytesPerSector();
	if (gl_diskSectorSize == 0) { return -1; }
	printm("Disk block [%u]byte\n", gl_diskSectorSize);

	printm("***** ******* *****\n");
	inited = TRUE;
	return 0;
}
static bool exist(char* fileAddr) {
	ASSERT(fileAddr == NULL, "invalid input");
	int err = 0;
	FILE_HANDLE hFile;
#ifdef WIN32
	hFile = CreateFileA(
		fileAddr,
		GENERIC_ALL, //GENERIC_READ | GENERIC_WRITE,  // 访问模式，可以根据需要设置
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, //FILE_SHARE_READ,               // 共享模式，0 表示独占访问
		NULL,										// 安全描述符，可以为 NULL
		OPEN_EXISTING, //OPEN_ALWAYS,								// 打开现有文件
		/*
		CREATE_NEW：如果文件不存在，则创建新文件；如果文件已经存在，函数会失败。
		CREATE_ALWAYS：创建新文件，如果文件已经存在，则覆盖原文件。
		OPEN_EXISTING：只打开已经存在的文件，如果文件不存在，函数会失败。
		OPEN_ALWAYS：打开已经存在的文件，如果文件不存在则创建新文件。
		TRUNCATE_EXISTING：打开文件，将文件大小截短为0字节，如果文件不存在，函数会失败。
		*/
		FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING,	// 文件属性，可以根据需要设置
		NULL										// 模板文件句柄，可以为 NULL
	);

	if (hFile == INVALID_HANDLE_VALUE) {
		return FALSE;
	}
#else
	hFile = open(fileAddr,
		O_RDONLY | O_SYNC | O_DIRECT | O_EXCL | O_CREAT,
		/*
		O_RDONLY：以只读方式打开文件。
		O_WRONLY：以只写方式打开文件。
		O_RDWR：以读写方式打开文件。
		O_CREAT：如果文件不存在，则创建一个新文件。
		O_EXCL：与O_CREAT一起使用，确保创建新文件，如果文件已存在则返回错误。
		O_TRUNC：如果文件已存在，将其截断为空文件。
		O_APPEND：以追加方式打开文件，即文件指针在文件末尾。
		O_NONBLOCK：以非阻塞方式打开文件。
		O_SYNC：以同步写入方式打开文件，要求数据写入文件后才返回。
		O_DIRECT: 绕过内核缓存，直接磁盘IO
		*/
		0
	);
	if (hFile == -1) {
		perror("open");
		printm("FILE_OpenFileByAPI error\n");
		return FALSE;
	}
#endif // WIN32

	closeFile(hFile);
	return TRUE;
}
uint64 getBlockSize() {
	return gl_diskSectorSize;
}
int openFile_DiskIO_sync(char* fileAddr, FILE_HANDLE* hFile) {
	ASSERT(fileAddr == NULL || hFile == NULL, "invalid input");
	int err = 0;
#ifdef WIN32
	* hFile = CreateFileA(
		fileAddr,
		GENERIC_ALL, //GENERIC_READ | GENERIC_WRITE,  // 访问模式，可以根据需要设置
		FILE_SHARE_READ| FILE_SHARE_WRITE| FILE_SHARE_DELETE, //FILE_SHARE_READ,               // 共享模式，0 表示独占访问
		NULL,										// 安全描述符，可以为 NULL
		OPEN_ALWAYS, //OPEN_ALWAYS,								// 打开现有文件
		/*
		CREATE_NEW：如果文件不存在，则创建新文件；如果文件已经存在，函数会失败。
		CREATE_ALWAYS：创建新文件，如果文件已经存在，则覆盖原文件。
		OPEN_EXISTING：只打开已经存在的文件，如果文件不存在，函数会失败。
		OPEN_ALWAYS：打开已经存在的文件，如果文件不存在则创建新文件。
		TRUNCATE_EXISTING：打开文件，将文件大小截短为0字节，如果文件不存在，函数会失败。
		*/
		FILE_FLAG_WRITE_THROUGH|FILE_FLAG_NO_BUFFERING,	// 文件属性，可以根据需要设置
		NULL										// 模板文件句柄，可以为 NULL
	);

	if (*hFile == INVALID_HANDLE_VALUE) {
		return -2;
	}
#else
#ifndef O_DIRECT
#define O_DIRECT	00040000
#endif // !O_DIRECT
	*hFile = open(fileAddr,
		O_RDWR | O_SYNC | O_CREAT | O_DIRECT,
		/*
		O_RDONLY：以只读方式打开文件。
		O_WRONLY：以只写方式打开文件。
		O_RDWR：以读写方式打开文件。
		O_CREAT：如果文件不存在，则创建一个新文件。
		O_EXCL：与O_CREAT一起使用，确保创建新文件，如果文件已存在则返回错误。
		O_TRUNC：如果文件已存在，将其截断为空文件。
		O_APPEND：以追加方式打开文件，即文件指针在文件末尾。
		O_NONBLOCK：以非阻塞方式打开文件。
		O_SYNC：以同步写入方式打开文件，要求数据写入文件后才返回。
		O_DIRECT: 绕过内核缓存，直接磁盘IO
		*/
		0
	);
	if (*hFile == -1) {
		perror("open");
		printm("FILE_OpenFileByAPI error\n");
		return -2;
	}
#endif // WIN32

	return 0;
}
int moveSeek(FILE_HANDLE hFile, int64 seek, int origin) {
	//SEEK_SET SEEK_CUR SEEK_END
	// 偏移量，可以是正数或负数
	int err = 0;
	ASSERT(seek < 0, "invalid seek: %lld\n", seek);
#ifdef WIN32
	DWORD dwMoveMethod = FILE_BEGIN;// FILE_BEGIN; // 从文件开头开始移动，也可以使用 FILE_CURRENT 或 FILE_END
	switch (origin) {
	case SEEK_SET:
		dwMoveMethod = FILE_BEGIN;
		break;
	case SEEK_CUR:
		dwMoveMethod = FILE_CURRENT;
		break;
	case SEEK_END:
		dwMoveMethod = FILE_END;
		break;
	default:
		return -2;
	}
	
	LARGE_INTEGER offset = {.QuadPart = seek};
	// 使用 SetFilePointer 进行移动
	DWORD dwNewPointer = SetFilePointer(hFile, offset.LowPart, &offset.HighPart, dwMoveMethod);

	if (dwNewPointer != INVALID_SET_FILE_POINTER) {
		// 移动成功
		return 0;
	}
	else {
		// 移动失败，可以使用 GetLastError() 获取错误信息
		printm("moveSeek error: [%d]\n", getSystemErrCode());
		return -2;
	}
#else

	int64 new_position = lseek(hFile, seek, origin);
	if (new_position == -1) {
		printm("moveSeek error: [%d]\n", getSystemErrCode());
		return -2;
	}
#endif // WIN32
	return 0;
}
int closeFile(FILE_HANDLE hFile) {
#ifdef WIN32
	return CloseHandle(hFile)?0:-1;
#else
	return close(hFile);
#endif // WIN32
}
int readFile_DiskIO_sync(FILE_HANDLE hFile, char* buff, uint64 len) {
	int err = 0;
	uint64 res = 0;
#ifdef WIN32
	err = ReadFile(hFile, buff, len, &res, NULL); if (err) { err = 0; }
	else { 
		err = -1; 
	}
#else
	err = read(hFile, buff, len); if (err > 0) { err = 0; }
	else {err = -1}
#endif // WIN32
	
	return err;
}
int writeFile_DiskIO_sync(FILE_HANDLE hFile, char* buff, uint64 len) {
	int err = 0;
	uint64 res = 0;
#ifdef WIN32
	OVERLAPPED Overlapped = { 0 }; //memcpy(Overlapped.Pointer, &seek, sizeof(seek)); 
	err = WriteFile(hFile, buff, len, &res, NULL);
#else
	//err = lseek(hFile, seek, SEEK_SET); if (err < 0) { return -1; }
	err = write(hFile, buff, len); 
#endif // WIN32
	if (err > 0) { err = 0; }
	else { err = -1; }
	return err;
}
int removeFile(char* addr) {
	ASSERT(addr == NULL, "invalid input");
	return remove(addr);
}
uint64 getOpenedFileSize(FILE_HANDLE hFile) {
	uint64 size = 0;
	int err = 0;
#ifdef WIN32
	err = GetFileSizeEx(hFile, &size); if (err == 0) {	size = U64_MAX; }
#else
	size = lseek(hFile, 0, SEEK_END); if (size == -1) { size = U64_MAX; }
#endif // WIN32

	ASSERT(size == U64_MAX, "get file size error");
	return size;
}
int createDirectory(char* path) {
	ASSERT(!path, "null directory");
#ifdef WIN32
	return !CreateDirectoryA(path, NULL);
#else
	mkdir(path, 0755);
	return 0;
#endif // WIN32
	
}

#ifdef WIN32
int FILE_CreateMamMap(HANDLE* mapping, HANDLE f, uint64 mapSize) {
	if (mapping == NULL || f == INVALID_API_FILE_HANDLE) {
		return -1;
	}

	// 创建内存映射文件对象
	(*mapping) = CreateFileMapping(
		f,              // 文件句柄
		NULL,               // 安全属性
		PAGE_READWRITE,     // 读写权限
		(uint32)(mapSize >> 32),                  // 高位文件大小
		(uint32)mapSize,                  // 低位文件大小
		NULL                // 映射对象名
	);

	if ((*mapping) == NULL) {
		printm("无法创建内存映射文件对象 [%d]\n", getSystemErrCode());
		return -3;
	}
	return 0;
}
#else
int FILE_CreateMamMap(char** mapping, int f, uint64 mapSize, uint64 seek) {
	// 创建文件到内存的映射
	void* addr = mmap(NULL, mapSize, PROT_READ | PROT_WRITE, MAP_SHARED, f, seek);
	if (addr == MAP_FAILED) {
		perror("mmap");
		return -2;
	}
	(*mapping) = (char*)addr;
	return 0;
}
#endif // WIN32


#ifdef WIN32
int FILE_OpenFileByAPI(char* fileAddr, DWORD openType, DWORD sharMode, DWORD createType, HANDLE* hFile) {
	if (fileAddr == NULL || hFile == NULL)
		return -1;
	if (*hFile != INVALID_API_FILE_HANDLE) {
		return -1;
	}

	*hFile = CreateFileA(
		fileAddr,
		openType, //GENERIC_READ | GENERIC_WRITE,  // 访问模式，可以根据需要设置
		sharMode, //FILE_SHARE_READ,               // 共享模式，0 表示独占访问
		NULL,										// 安全描述符，可以为 NULL
		createType, //OPEN_ALWAYS,								// 打开现有文件
		/*
		CREATE_NEW：如果文件不存在，则创建新文件；如果文件已经存在，函数会失败。
		CREATE_ALWAYS：创建新文件，如果文件已经存在，则覆盖原文件。
		OPEN_EXISTING：只打开已经存在的文件，如果文件不存在，函数会失败。
		OPEN_ALWAYS：打开已经存在的文件，如果文件不存在则创建新文件。
		TRUNCATE_EXISTING：打开文件，将文件大小截短为0字节，如果文件不存在，函数会失败。
		*/
		FILE_ATTRIBUTE_NORMAL,						// 文件属性，可以根据需要设置
		NULL										// 模板文件句柄，可以为 NULL
	);

	if (*hFile == INVALID_HANDLE_VALUE) {
		return -2;
	}

	return 0;
}
#else
int FILE_OpenFileByAPI(char* fileAddr, int flags, int* f) {
	if (fileAddr == NULL || f == NULL) {
		return -1;
	}
	*f = open(fileAddr,
		flags,
		/*
		O_RDONLY：以只读方式打开文件。
		O_WRONLY：以只写方式打开文件。
		O_RDWR：以读写方式打开文件。
		O_CREAT：如果文件不存在，则创建一个新文件。
		O_EXCL：与O_CREAT一起使用，确保创建新文件，如果文件已存在则返回错误。
		O_TRUNC：如果文件已存在，将其截断为空文件。
		O_APPEND：以追加方式打开文件，即文件指针在文件末尾。
		O_NONBLOCK：以非阻塞方式打开文件。
		O_SYNC：以同步写入方式打开文件，要求数据写入文件后才返回。
		*/
		0644
	);
	if (*f == -1) {
		perror("open");
		printm("FILE_OpenFileByAPI error\n");
		return -2;
	}
	return 0;
}
#endif // WIN32

static int openFile(char* addr, FileOpenType openType, FILE_HANDLE* handler) {
	if (handler == NULL)
		return -1;
	int err = 0;
#ifdef WIN32
	err = FILE_OpenFileByAPI(addr, openType, FILE_SHARE_READ, OPEN_ALWAYS, handler);
#else
	err = FILE_OpenFileByAPI(addr, openType | O_CREAT, handler);
#endif // WIN32
	if (err < 0) {
		printm("open file fail [%d]\n", getSystemErrCode());
		return -1;
	}
	return 0;
}
static int resetFileSize(FILE_HANDLE f, uint64 newSize) {
	int err = 0;
#ifdef WIN32
	err = moveSeek(f, newSize, SEEK_SET);
	if (!err) {
		if (SetEndOfFile(f)) {
			err = 0;
		}
		else {
			err = -1;
			ASSERT(TRUE, "SetEndOfFile fail\n");
		}
	}
	
#else
	err = ftruncate(f, newSize);
#endif // WIN32
	return err;
}


#ifdef WIN32
void FILE_CloseMemMap(HANDLE mapping, char* ptr) {
	bool res = UnmapViewOfFile(ptr);
	ASSERT(!res, "FILE_CloseMemMap error: UnmapViewOfFile fail");

	CloseHandle(mapping);
	mapping = NULL;
}
#else
void FILE_CloseMemMap(char* ptr, uint64 size) {
	// 解除映射并关闭文件
	munmap(ptr, size);
}
#endif // WIN32

int FILE_RemoveFile(char* FileAddr) {
	return remove(FileAddr);
}

#ifdef WIN32
int FILE_LoadFileToMemMap(HANDLE* mapping, uint64 seek, uint64 size, char** outFilePtr) {
	if (mapping == NULL || outFilePtr == NULL)
		return -1;
	uint32 high = seek >> 32;
	uint32 low = seek;
	// 映射文件内容到内存
	(*outFilePtr) = MapViewOfFile(*mapping, FILE_MAP_ALL_ACCESS, (uint32)(seek >> 32), (uint32)seek, size);

	if ((*outFilePtr) == NULL) {
		printm("无法映射文件到内存\n");
		return -5;
	}
	return 0;
}
void FILE_MemMapFlushByHandle(HANDLE f) {
	// 刷新文件缓冲
	FlushFileBuffers(f);
}
#endif // WIN32

void FILE_MemMapFlushByByte(char* ptr, uint64 size) {
#ifdef WIN32
	// 刷新内存映射
	FlushViewOfFile(ptr, size);  // ptr为内存映射的指针，size为要刷新的数据大小
#else
	// 同步映射的数据到文件
	msync(ptr, size, MS_SYNC);
#endif // WIN32

}

int unmapFile(char* ptr, uint64 size) {
	int err = 0;
#ifdef WIN32
	bool res = UnmapViewOfFile(ptr);
	ASSERT(!res, "unmap error");
#else
	err = munmap(ptr, size);
	ASSERT(err<0, "unmap error");
#endif // WIN32
	return 0;
}
int mapFile(FILE_HANDLE f, FILE_HANDLE* m, uint64 size, char** ptr) {
	int err = 0;
#ifdef WIN32
	//it will fail when open same name file second time if "size" param is zero
	err = FILE_CreateMamMap(m, f, size);	if (err < 0) { return -1; }
	err = FILE_LoadFileToMemMap(m, 0, size, ptr);
#else
	err = FILE_CreateMamMap(ptr, f, size, seek);
#endif // WIN32
	if (err < 0) { return -1; }

	return 0;
}
int64 decodeOpenType(FileOpenType openType) {
	int64 type = 0;
	switch (openType)
	{
#ifdef WIN32
	case ONLY_READ:
		type = GENERIC_READ;
		break;
	case ONLY_WRITE:
		type = GENERIC_WRITE;
		break;
	case EXECUTE:
		type = GENERIC_EXECUTE;
		break;
	case ALL_PER:
		type = GENERIC_ALL;
		break;
	case SYNC_IO:
		type = SYNC_IO;
		break;
	
#else
	case ONLY_READ:
		type = O_RDONLY;
		break;
	case ONLY_WRITE:
		type = O_WRONLY;
		break;
	case EXECUTE:
		type = EXECUTE;
		break;
	case ALL_PER:
		type = O_WRONLY | O_RDONLY;
		break;
	case SYNC_IO:
		type = O_SYNC;
		break;
#endif // WIN32
	default:
		break;
	}

	return type;
}
int openFileAndMap(char* addr, FileOpenType openType, FileHandler* fh, uint64 size, uint64 seek) {
	if (addr == NULL || fh == NULL)
		return -1;
	int err = 0;

	err = openFile(addr, openType, &fh->f); if (err < 0) { return -1; }
#ifdef WIN32
	err = mapFile(fh->f, &fh->m, seek, size, &fh->ptr); if (err < 0) { return -1; }
#else
	err = mapFile(fh->f, NULL, seek, size, &fh->ptr); if (err < 0) { return -1; }
#endif // WIN32

	fh->mapSize = size;
	return 0;
}
int remap(FileHandler* fh, uint64 seek, uint64 size) {
	int err = 0;
	err = unmapFile(fh->ptr, fh->mapSize); if (err < 0) { return -1; }
#ifdef WIN32
	err = mapFile(fh->f, &fh->m, seek, size, &fh->ptr); if (err < 0) { return -1; }
#else
	err = mapFile(fh->f, NULL, seek, size, &fh->ptr); if (err < 0) { return -1; }
#endif // WIN32

	return 0;
}
int closeFileMem(FileHandler* fh) {
	ASSERT(!fh, "invalid input");
	
#ifdef WIN32
	FILE_CloseMemMap(&fh->m, fh->ptr);
#else
	FILE_CloseMemMap(fh->ptr, fh->mapSize);
#endif // WIN32

}
void removeFileMem(char* addr) {
	FILE_RemoveFile(addr);
}
int flushDisk(FileHandler* fh, uint64 offset, uint64 size) {
	ASSERT(!fh, "invalid input");
	FILE_MemMapFlushByByte(fh->ptr+offset, size);
}

void testStorageHandler() {
	int err = 0;
	const uint64 blockSize = getBlockSize();
	char addr[FILE_ADDR_MAX_LEN] = "C:\\myWorkspace\\test\\temp\\tt.txt";

	FileHandler fh = { 0 }; memcpy(fh.addr, addr, strlen(addr));

	err = openFileAndMap(fh.addr, ALL_PER, &fh, BYTE_1K*4, 0);
	if (err < 0) {
		ERROR(-1);
	}
	char* ptr = fh.ptr;
	for (uint32 i = 0; i < 1024;i++, ptr+= sizeof(i)) {
		memcpy(ptr, &i, sizeof(i));
	}
	flushDisk(&fh, 0, ptr - fh.ptr);
	closeFileMem(&fh);


	err = openFileAndMap(fh.addr, ONLY_READ, &fh, BYTE_1K * 4, 0);
	if (err < 0) {
		ERROR(-2);
	}
	uint32* num = fh.ptr;
	for (uint32 i = 0; i < 1024; i++, num++) {
		printm("[%u]",*num);
	}
	printm("\n");


	removeFileMem(fh.addr);
	return;

error:
	err;
	printm("test fail\n");
	return;
}

void testDiskIO() {
	int err = 0;

	initFileIO();
	const uint64 blockSize = getBlockSize();
	char buff[BYTE_1K * 4] = { 0 };

	uint64 size = sizeof(buff);
	for (uint64 i = 0; i < size;i++) {
		buff[i] = i;
	}
	char addr[FILE_ADDR_MAX_LEN] = "C:\\myWorkspace\\test\\temp\\tt.txt";
	FILE_HANDLE f;
	err = openFile_DiskIO_sync(addr, &f); if (err < 0) { ERROR(-1) }
	err = writeFile_DiskIO_sync(f, buff, size); if (err < 0) { ERROR(-2) }
	
	char tmp[FILE_ADDR_MAX_LEN] = { 0 };
	err = moveSeek(f, 0, SEEK_SET); if (err < 0) { ERROR(-3) }
	err = readFile_DiskIO_sync(f, tmp, FILE_ADDR_MAX_LEN); if (err < 0) { ERROR(-4) }

	closeFile(f);
	printm("IO success %s\n",tmp);
	return;
error:
	printm("testDiskIO fail [%d] err[%d]\n", err, getSystemErrCode());
	return;
}

static int fileId2Str(uint64 fileId, char* str, uint32 len) {
	ASSERT(fileId == U64_MAX || str == NULL || len == 0, "invalid input");
	int err = 0;
	err = base10ToBase59(fileId, str, len);
	return err;
}

int FILEIO_Init() {
	return initFileIO();
}
bool FILEIO_Exist(char* addr) {
	return exist(addr);
}
int FILEIO_Open(char* fileAddr, FILE_HANDLE* hFile) {
	return openFile_DiskIO_sync(fileAddr, hFile);
}
int FILEIO_MoveSeek(FILE_HANDLE hFile, int64 seek, int origin) {
	return moveSeek(hFile, seek, origin);
}
int FILEIO_CloseFile(FILE_HANDLE hFile) {
	return closeFile(hFile);
}
int FILEIO_Read(FILE_HANDLE hFile, char* buff, uint64 len) {
	gl_fileReadCnt++;
	return readFile_DiskIO_sync(hFile, buff, len);
}
int FILEIO_Write(FILE_HANDLE hFile, char* buff, uint64 len) {
	gl_fileWriteCnt++;
	return writeFile_DiskIO_sync(hFile, buff, len);
}
uint64 FILEIO_GetBlockSize() {
	return getBlockSize();
}
int FILEIO_Remove(char* addr) {
	return removeFile(addr);
}
int FILEIO_Rename(char* addr, char* dest) {
	return rename(addr, dest);
}
uint64 FILEIO_GetFileSize(FILE_HANDLE hFile) {
	return getOpenedFileSize(hFile);
}
int FILEIO_ResetFileSize(FILE_HANDLE f, uint64 newSize) {
	return resetFileSize(f, newSize);
}
int FILEIO_CreateDirectory(char* path) {
	return createDirectory(path);
}

//------ memory map file
int FILEIO_MemoryMap(FILE_HANDLE f, FILE_HANDLE* m, uint64 size, char** ptr) {
	ASSERT(f == INVALID_FILE_HANDLE || m == NULL || size == 0 || ptr == NULL, "invalid input");
	ASSERT(*m != INVALID_FILE_HANDLE || *ptr != NULL, "invalid input");
	return mapFile(f, m, size, ptr);
}
void FILEIO_MemoryMapClose(FILE_HANDLE m, char* ptr, uint64 size) {
	ASSERT(m == INVALID_FILE_HANDLE || size == 0 || ptr == NULL, "invalid input");
#ifdef WIN32
	FILE_CloseMemMap(m, ptr);
#else
	FILE_CloseMemMap(ptr, size);
#endif // WIN32
}
void FILEIO_MemoryFlush(char* ptr,uint64 size) {
	ASSERT(ptr == NULL || size == 0, "invalid input");
	FILE_MemMapFlushByByte(ptr, size);
}
int FILEIO_MemoryReMap(FILE_HANDLE f, FILE_HANDLE* m, uint64 curSize, uint64 size, char** ptr) {
	ASSERT(f == INVALID_FILE_HANDLE || m == NULL || curSize == 0 || size == 0 || ptr == NULL, "invalid input");
	ASSERT(*m == NULL || *ptr == NULL, "invalid input");
	FILEIO_MemoryMapClose(*m, *ptr, curSize);
	*m = INVALID_FILE_HANDLE;
	*ptr = NULL;
	return FILEIO_MemoryMap(f, m, size, ptr);
}
//--------------------------

void testFileIO() {
	int err = 0;
	err = FILEIO_Init(); if (err < 0) { ERROR(-1); }

	return;
error:

	return ;
}
void testFileMapIO() {
	/*
	* 大文件单次
	*写 差不多，map略慢一点
	*读 map慢几乎一倍
	* 
	* 大文件分批写，单次4K
	* 写 差不多，map略慢一点
	* 读 map慢很多。1G-> 1s 145ms 436us | 86ms 467us
	* 
	* 直接磁盘IO 单次4K 约800us
	*/
	int err = 0;
	err = FILEIO_Init(); if (err < 0) { ERROR(-1); }
	FILE_HANDLE f;
	FILE_HANDLE m;
	char* ptr = NULL;
	uint64 totalTime = 0;
	uint64 mapSize = BYTE_1G;
	uint64 fileSize = 0;
	bool exist = FILEIO_Exist(TEST_ADDR);
	char* buff = malloc(mapSize); ASSERT(buff == NULL, "malloc fail")
	uint64 cnt = 256 * 1024;
	char* tmpBuff = buff;
	uint64 flushSize = BYTE_1K * 4;

	printm("\n--- memory map io ---\n");
	time_t time = getMicrosecondTick();
	err = FILEIO_Open(TEST_ADDR, &f); if (err < 0) { ERROR(-2); }
	time = getMicrosecondTick() - time; //totalTime += time;
	//printm("open: "); printUsTick(time); printm("\n");
	
	time = getMicrosecondTick();
	err = FILEIO_MemoryMap(f, &m, mapSize, &ptr); if (err < 0) { ERROR(-2); }
	time = getMicrosecondTick() - time; //totalTime += time;
	//printm("map: "); printUsTick(time); printm("\n");
	
	time = getMicrosecondTick();
	tmpBuff = ptr;
	for (uint64 i = 0; i < cnt;i++, tmpBuff += flushSize) {
		memset(tmpBuff, 0xfa, flushSize);
		FILEIO_MemoryFlush(tmpBuff, flushSize);
	}
	time = getMicrosecondTick() - time; totalTime += time;
	printm("write and flush 1G: "); printUsTick(time); printm("\n");

	time = getMicrosecondTick();
	memcpy(buff, ptr, mapSize);
	time = getMicrosecondTick() - time; totalTime += time;
	printm("read 1G: "); printUsTick(time); printm("\n");
	
	time = getMicrosecondTick();
	FILEIO_MemoryMapClose(m, ptr, mapSize);
	time = getMicrosecondTick() - time; //totalTime += time;
	//printm("unmap 1G: "); printUsTick(time); printm("\n");
	
	time = getMicrosecondTick();
	fileSize = FILEIO_GetFileSize(f); ASSERT(fileSize == U64_MAX, "file size error");
	time = getMicrosecondTick() - time; //totalTime += time;
	//printm("get size 1G: "); printUsTick(time); printm("\n");
	//printm("size: "); printBytes(fileSize); printm("\n");
	printm("total time: "); printUsTick(totalTime); printm("\n");
	FILEIO_CloseFile(f);
	FILEIO_Remove(TEST_ADDR);

	totalTime = 0;
	//********************************************************************
	printm("\n--- memory map io ---\n");
	
	memset(buff, 0xfa, mapSize);
	time = getMicrosecondTick();
	err = FILEIO_Open(TEST_ADDR, &f); if (err < 0) { ERROR(-2); }
	time = getMicrosecondTick() - time; //totalTime += time;
	//printm("open: "); printUsTick(time); printm("\n");

	//time = getMicrosecondTick();
	//err = FILEIO_Write(f, buff, mapSize);
	//time = getMicrosecondTick() - time; totalTime += time;
	//printm("write 1G: "); printUsTick(time); printm("\n");

	time = getMicrosecondTick();
	tmpBuff = buff;
	printm("\n\n*************\n\n");
	for (uint64 i = 0; i < cnt;i++, tmpBuff += flushSize) {
		//time_t wrTime = getMicrosecondTick();
		err = FILEIO_Write(f, tmpBuff, flushSize);
		//单次4K io ~= 800us
		//wrTime = getMicrosecondTick() - wrTime;
		//printm("%llu\n", wrTime);
	}
	time = getMicrosecondTick() - time; totalTime += time;
	printm("cycle write 256*1024 time: "); printUsTick(time); printm("\n");

	time = getMicrosecondTick();
	err = FILEIO_Read(f, buff, mapSize); ASSERT(err < 0,"read fail");
	time = getMicrosecondTick() - time; totalTime += time;
	printm("read 1G: "); printUsTick(time); printm("\n");

	time = getMicrosecondTick();
	fileSize = FILEIO_GetFileSize(f); ASSERT(fileSize == U64_MAX, "file size error");
	time = getMicrosecondTick() - time; //totalTime += time;
	//printm("get size 1G: "); printUsTick(time); printm("\n");
	//printm("size: "); printBytes(fileSize); printm("\n");
	printm("total time: "); printUsTick(totalTime); printm("\n");
	FILEIO_CloseFile(f);
	FILEIO_Remove(TEST_ADDR);
	return;
error:
	ASSERT(TRUE,"err");
	return;
}
void testFileWriteLatency() {
	/*
	数据量越大，磁盘映像文件的写入速度越慢。
	而直接磁盘IO速度几乎无变化
	
	write 1G
	mmap write: ~= 1.3s
	disk write: ~= 1s

	rename: ~= 1ms


	write 单笔 5G
	mmap write: ~= 11s	//应该是用户态内存cp到内核态的时候耗费时间，另外可能存在不确定的io处理
	disk write: ~= 1.1s

	write 单笔 512(block size)
	mmap write: ~= 1 ms	
	disk write: ~= 500 us

	

	rename: ~= 1ms
	*/
	int err = 0;
	err = FILEIO_Init();
	uint64 size = BYTE_1K*32;
	printm("mallocing buffer\n");
	char* data = malloc(size); if (!data) { ASSERT(TRUE,"malloc fail"); }
	memset(data, 1, size);
	uint64 tick = 0;
	printm("malloc buffer done, start test...\n");
	for (uint32 i = 0; i < 32;i++) {
		FILE_HANDLE f1 = INVALID_FILE_HANDLE;
		FILE_HANDLE m1 = INVALID_FILE_HANDLE;
		char* ptr = NULL;
		FILE_HANDLE f2 = INVALID_FILE_HANDLE;
		err = FILEIO_Open(TEST_ADDR, &f1); ASSERT(err < 0, "open fail");
		err = FILEIO_MemoryMap(f1, &m1, size, &ptr); ASSERT(err < 0, "map fail");
		tick = myCounterStart();
		memcpy(ptr, data, size);
		FILEIO_MemoryFlush(ptr, size);
		printm("mmap write "); printUsTick(myCounterUs(tick)); printm("\n");
		FILEIO_MemoryMapClose(m1, ptr, size);
		FILEIO_CloseFile(f1);
		FILEIO_Remove(TEST_ADDR);

		err = FILEIO_Open(TEST_ADDR, &f2); ASSERT(err < 0, "open fail");
		tick = myCounterStart();
		err = FILEIO_Write(f2, data, size); ASSERT(err < 0, "write fail");
		printm("disk write "); printUsTick(myCounterUs(tick)); printm("\n");
		FILEIO_CloseFile(f2);
		tick = myCounterStart();
		err = FILEIO_Rename(TEST_ADDR, TEST_ADDR2); ASSERT(err < 0, "rename fail");
		printm("rename "); printUsTick(myCounterUs(tick)); printm("\n\n");
		FILEIO_Remove(TEST_ADDR2);
	}

	return;
}
void testFileRight() {
	int err = 0;
	char* addr = "C:\\tmp\\tt!";
	FILE_HANDLE f = INVALID_FILE_HANDLE;
	err = FILEIO_Open(addr, &f);
	ASSERT(err, "open file fail");
	char buff[1024] = { 1 };
	err = FILEIO_Write(f, buff, sizeof(buff));
	ASSERT(err, "write file fail");

	printm("File right test pass\n");
}
void testFile_test2_diffFileHandle() {
	//diffrent file handle write and read

	FILE_HANDLE f1 = INVALID_FILE_HANDLE;
	FILE_HANDLE f2 = INVALID_FILE_HANDLE;
	int err = 0;
	char* addr = "C:\\tmp\\test.txt";
	char buff[1024] = { 1 };
	char buff2[1024] = { 0 };
	err = FILEIO_Open(addr, &f1);
	err = FILEIO_Open(addr, &f2);

	err = FILEIO_MoveSeek(f1, 0, SEEK_SET);
	err = FILEIO_Write(f1, buff, 1024);

	err = FILEIO_MoveSeek(f2, 0, SEEK_SET);
	err = FILEIO_Read(f2, buff2, 1024);
	
	ASSERT(memcmp(buff, buff2, 1024) != 0, "not same");

	return;
}