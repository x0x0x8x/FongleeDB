
#define DEBUG_DISK_LOG

#define DISK_LOG_PATH "C:\\tmp\\out\\"
#define DISK_LOG_QUEUE_LEN_MAX (32U)
#define DISK_LOG_BUFFER_LEN_MAX (32U)
#define DISK_LOG_FILE_BLOCK_SIZE (4096U)


int initDiskLog();
void printLog(const char* format, ...);
void DiskLogWaitAllDone();	//pending until all flush done

void test_disklog();