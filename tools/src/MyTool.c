#include "MyTypes.h"
#include "MyTool.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h> // For Sleep and console functions
#include <Winsock2.h>
#pragma comment (lib, "ws2_32.lib")
#else
#include <sys/sysinfo.h>
#include <arpa/inet.h>
#include <errno.h>
#endif // WIN32

#pragma warning(disable : 4996)

//小端是逆字节序
bool isSmallEndian() {
	unsigned int value = 0x01020304;
	unsigned char* ptr = (unsigned char*)&value;

	if (ptr[0] == 0x01 && ptr[1] == 0x02 && ptr[2] == 0x03 && ptr[3] == 0x04) {
		// 大端字节序
		return TRUE;
	}
	else if (ptr[0] == 0x04 && ptr[1] == 0x03 && ptr[2] == 0x02 && ptr[3] == 0x01) {
		// 小端字节序
		return FALSE;
	}
	else {
		// 未知字节序
		printf("字节序判断失败\n");
		exit(0);
	}

}
void printBit(int p) { 
	int i = 0;        
	int bit1 = 1;        
	for (i = sizeof(p) * 8 - 1; i >= 0; i--) { 
		unsigned int x = (((bit1 << i) & p) != 0);                
		printf("%d", x); 
	}        
	//printf("\n "); 
}
int getSystemErrCode() {
	int ret = 0;
#ifdef WIN32
	ret = WSAGetLastError();
	return ret;
#else
	return errno;
#endif // WIN32
	


}
uint32 getU32RandomNumber() {
	// 设置种子值，一般使用当前时间作为种子
	int random2 = 0;

#ifdef WIN32
	random2 = 723;
#else
	/*
	struct sysinfo {
    long uptime;             // 系统运行时间
    unsigned long loads[3];  // 过去1分钟、5分钟和15分钟的平均负载
    unsigned long totalram;  // 总内存大小
    unsigned long freeram;   // 可用内存大小
    unsigned long sharedram; // 共享内存大小
    unsigned long bufferram; // 缓冲区大小
    unsigned long totalswap; // 总交换空间大小
    unsigned long freeswap;  // 可用交换空间大小
    unsigned short procs;    // 当前进程数量
    unsigned long totalhigh; // 高位内存大小
    unsigned long freehigh;  // 可用高位内存大小
    unsigned int mem_unit;   // 内存单元大小（字节）
    char _f[20-2*sizeof(long)-sizeof(int)]; // 保留字段
};

	*/

	struct sysinfo info;
	sysinfo(&info);
	random2 = info.loads[0];
#endif // WIN32

	srand(time(NULL) + random2);

	// 生成随机数
	uint32 randomNum = rand();
	return randomNum;
}

//10进制转换任意进制字符串(倒序)
int baseConversion(uint64 base10Num, uint32 destBase, char* out, uint64 outlen) {
	if (out == NULL)
		return -1;
	uint64 dec_num = base10Num;
	uint64 n = destBase;
	uint64 remainer;
	static const char validChar[85] = { '!','#','$','%','&','\'','(',')','+',',','-','0','1','2','3','4','5','6','7','8','9',';','=','@','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z','[',']','^','_','`','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z','{','}','~',(char)127 };
	uint64 i = 0;
	while (dec_num != 0) {
		if (i > outlen) {
			return -2;
		}
		remainer = dec_num % n;
		out[i++] = remainer + '0';
		dec_num = dec_num / n;

	}
	out[i] = 0;
	//for (uint32 j = i - 1;; j--) {
	//	if (out[j] - '0' > 9) {
	//		out[j] = 'a' + (out[j] - '0' - 10);
	//	}
	//	printm("%c", out[j]);
	//	if (j == 0)
	//		break;
	//}
	//printm("\n");

	return 0;
}
//10进制转换59进制字符串(倒序)
int base10ToBase59(uint64 base10Num, char* base59, uint64 outlen) {
	if (base59 == NULL)
		return -1;
	uint64 dec_num = base10Num;
	const uint64 n = 59;
	uint64 remainer;
	static const char validChar[59] = { '!','#','$','%','&','\'','(',')','+',',','-','0','1','2','3','4','5','6','7','8','9',';','=','@','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z','[',']','^','_','`','{','}','~',(char)127 };
	static const char validChar2[85] = { '!','#','$','%','&','\'','(',')','+',',','-','0','1','2','3','4','5','6','7','8','9',';','=','@','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z','[',']','^','_','`','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z','{','}','~',(char)127 };
	uint64 i = 0;
	if (base10Num == 0) {
		base59[0] = validChar[0];
		i++;
	}

	while (dec_num != 0) {
		if (i > outlen) {
			return -2;
		}
		remainer = dec_num % n;
		base59[i++] = validChar[remainer];
		dec_num = dec_num / n;

	}
	base59[i] = 0;
	return 0;
}

//bit array by index
void setBitArrayByIndex(char* arr, uint64 index) {
	uint64* arr64 = arr;
	arr64 += (index / 64);
	uint8 i8 = (index % 64);
	BIT_SET(*arr64, 63 - i8);
}
void cleanBitArrayByIndex(char* arr, uint64 index) {
	uint64* arr64 = arr;
	arr64 += (index / 64);
	uint8 i8 = (index % 64);
	BIT_CLEAR(*arr64, 63 - i8);
}
bool checkBitArrayByIndex(char* arr, uint64 index) {
	uint64* arr64 = arr;
	arr64 += (index / 64);
	uint8 i8 = (index % 64);
	return BIT_CHECK(*arr64, 63-i8);
}
uint64 getBitArrayMaxIndex(char* arr, uint64 maxBit) {
	int err = 0;
	if (!arr || maxBit == 0) { return U64_MAX; }
	uint64 ret = U64_MAX;
	for (uint64 i = maxBit - 1;; i--) {
		if (checkBitArrayByIndex(arr, i)) {
			ret = i;
			break;
		}
		
		if (i == 0) {
			break;
		}
	}

	return ret;
}

void initMutex(Mutex* mutex) {
#ifdef _WIN32
	InitializeCriticalSection(&mutex->mutex);
#else
	pthread_mutex_init(&mutex->mutex, NULL);
#endif
}
void lockMutex(Mutex* mutex) {
#ifdef _WIN32
	EnterCriticalSection(&mutex->mutex);
#else
	pthread_mutex_lock(&mutex->mutex);
#endif
}
void unlockMutex(Mutex* mutex) {
#ifdef _WIN32
	LeaveCriticalSection(&mutex->mutex);
#else
	pthread_mutex_unlock(&mutex->mutex);
#endif
}
int tryLockMutex(Mutex* mutex) {
	if (mutex == NULL)
		return -1;
#ifdef _WIN32
	return TryEnterCriticalSection(&mutex->mutex) ? 0 : -2;
#else
	return (pthread_mutex_trylock(&mutex->mutex) == 0) ? 0 : -2;
#endif
}
void removeMutex(Mutex* mutex) {
#ifdef _WIN32
	DeleteCriticalSection(&mutex->mutex);
#else
	pthread_mutex_destroy(&mutex->mutex);
#endif
}

void printm(const char* format, ...) {
#ifdef DEBUG_ON
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	//vsnprintf(buff, size, format, args);	//输出到指定位置
	va_end(args);
#endif // DEBUG_ON

}
void assert_gl(bool condition, const char* format, ...) {
	if (!condition)
		return;
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	//vsnprintf(buff, size, format, args);	//输出到指定位置
	va_end(args);
	printf(" Err code[%d]\n", getSystemErrCode());
	exit(0);
}
void keybordInput(char* buff, uint32 len) {
	if (!buff || len == 0) { return; }
	fgets(buff, len, stdin);
}
void print_progress_bar(int percentage) {
	int bar_width = 50; // Width of the progress bar
	int pos = bar_width * percentage / 100;

	char progress_bar[51];
	memset(progress_bar, '#', pos);
	memset(progress_bar + pos, ' ', 51-pos);
	progress_bar[50] = '\0';
	printf("\r[%s] %d%%", progress_bar, percentage);
	fflush(stdout); // Ensure the output is displayed immediately
}
void hide_cursor() {
#ifdef WIN32
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(hConsole, &cursorInfo);
	cursorInfo.bVisible = FALSE; // Hide the cursor
	SetConsoleCursorInfo(hConsole, &cursorInfo);
#else
	printf("\033[?25l"); fflush(stdout);
#endif // WIN32
}
void show_cursor() {
#ifdef WIN32
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(hConsole, &cursorInfo);
	cursorInfo.bVisible = TRUE; // Show the cursor
	SetConsoleCursorInfo(hConsole, &cursorInfo);
#else
	printf("\033[?25h"); fflush(stdout);
#endif
}
