#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MyTime.h"
#include "MyTool.h"

#ifdef WIN32
#include "Windows.h"
#else
#include <sys/time.h>
#endif // WIN32


#pragma warning(disable : 4996)

#ifdef DEBUG
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG

#define TIME_BUFF_SIZE 128

static int64 gl_Frequency = 0;

time_t getCurTick() {
    time_t tick;
	time(&tick);
	return tick;
}

//单位: us 微秒
uint64 myCounterStart() {
    uint64 ret = 0;
#ifdef WIN32
    LARGE_INTEGER time_start;	//开始时间
    if (gl_Frequency == 0) {
        LARGE_INTEGER f;	//计时器频率
        QueryPerformanceFrequency(&f);
        gl_Frequency = (double)f.QuadPart;
}

    QueryPerformanceCounter(&time_start);	//计时开始
    memcpy(&ret, &time_start, sizeof(uint64));

    LARGE_INTEGER* tmp = &time_start;
    tmp = &ret;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ret = tv.tv_sec * 1000 * 1000 + tv.tv_usec;
#endif // WIN32

    
    return ret;
}
uint64 myCounterUs(uint64 start) {
    int64 run_time;
#ifdef WIN32
    LARGE_INTEGER* time_start = &start;	//开始时间
    LARGE_INTEGER time_over;	//结束时间
    if (gl_Frequency == 0) {
        LARGE_INTEGER f;	//计时器频率
        QueryPerformanceFrequency(&f);
        gl_Frequency = (double)f.QuadPart;
    }
    QueryPerformanceCounter(&time_over);
    run_time = 1000000 * (time_over.QuadPart - time_start->QuadPart) / gl_Frequency;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    run_time = (tv.tv_sec * 1000 * 1000 + tv.tv_usec) - start;
#endif // WIN32

    return (uint64)run_time;
}
uint64 getMicrosecondTick() {
#ifdef WIN32
    LARGE_INTEGER tick; 
    QueryPerformanceCounter(&tick);
    return tick.QuadPart;
#else

#endif // WIN32

}
void printUsTick(uint64 us) {
    uint64 h, m, s, ms;
    h = us / 3600000000LLU; us %= 3600000000LLU;
    m = us / 60000000LLU;   us %= 60000000LLU;
    s = us / 1000000LLU;    us %= 1000000LLU;
    ms = us / 1000LLU;      us %= 1000LLU;
    /*
    Hours, minutes, seconds, milliseconds, microseconds
    */
    if (h) {
        printf("%lluh ", h);
    }
    if (m) {
        printf("%llum ", m);
    }
    if (s) {
        printf("%llus ", s);
    }
    if (ms) {
        printf("%llums ", ms);
    }
    printf("%lluus ", us);
}
void printUsToBuff(char* buff, uint32 len, uint64 us) {
    int err = 0;
    char* ptr = buff;
    uint64 h, m, s, ms;
    h = us / 3600000000LLU; us %= 3600000000LLU;
    m = us / 60000000LLU;   us %= 60000000LLU;
    s = us / 1000000LLU;    us %= 1000000LLU;
    ms = us / 1000LLU;      us %= 1000LLU;
    /*
    Hours, minutes, seconds, milliseconds, microseconds
    */
    if (h) {
        err = sprintf(ptr, "%lluh ", h); ptr += err;
    }
    if (m) {
        err = sprintf(ptr,"%llum ", m); ptr += err;
    }
    if (s) {
        err = sprintf(ptr,"%llus ", s); ptr += err;
    }
    if (ms) {
        err = sprintf(ptr,"%llums ", ms); ptr += err;
    }
    err = sprintf(ptr,"%lluus ", us); ptr += err;
    return;
}

int tickToYMD(time_t tick, char* buff, uint32 len) {
    int err = 0;
    struct tm* gl_time = localtime(&tick);
    char* ptr = buff;
#define REMAIN_LEN (len - (ptr - buff))
    if (gl_time->tm_year > 0) {
        err = snprintf(ptr, REMAIN_LEN, "Y: %d ", gl_time->tm_year + 1900);
        if (err == 0) { return -1; }
        ptr += err;
    }
    if (gl_time->tm_mon >= 0) {
        err = snprintf(ptr, REMAIN_LEN, "M: %d ", gl_time->tm_mon + 1);
        if (err == 0) { return -1; }
        ptr += err;
    }
    if (gl_time->tm_mday > 0) {
        err = snprintf(ptr, REMAIN_LEN, "D: %d", gl_time->tm_mday);
        if (err == 0) { return -1; }
        ptr += err;
    }

    // Add hours, minutes, seconds, and milliseconds
    if (gl_time->tm_hour > 0 || gl_time->tm_min > 0 || gl_time->tm_sec > 0) {
        err = snprintf(ptr, REMAIN_LEN, " H: %02d M: %02d S: %02d", gl_time->tm_hour, gl_time->tm_min, gl_time->tm_sec);
        if (err == 0) { return -1; }
        ptr += err;
    }

    // Add milliseconds
    // You will need to provide the milliseconds value here.
    int milliseconds = 123; // Replace with actual milliseconds value
    if (milliseconds > 0) {
        err = snprintf(ptr, REMAIN_LEN, " MS: %03d", milliseconds);
        if (err == 0) { return -1; }
        ptr += err;
    }

    return 0;
}
int tickToYMD2(time_t tick, char* buff, uint32 len) {
    //201408151026
    int err = 0;
    struct tm* gl_time = localtime(&tick);
    char* ptr = buff;
#define REMAIN_LEN (len - (ptr - buff))
    if (gl_time->tm_year > 0) {
        err = snprintf(ptr, REMAIN_LEN, "%d", gl_time->tm_year + 1900);
        if (err == 0) { return -1; }
        ptr += err;
    }
    if (gl_time->tm_mon >= 0) {
        err = snprintf(ptr, REMAIN_LEN, "%d", gl_time->tm_mon + 1);
        if (err = 0) { return -1; }
        ptr += err;
    }
    if (gl_time->tm_mday > 0) {
        err = snprintf(ptr, REMAIN_LEN, "%d", gl_time->tm_mday);
        if (err = 0) { return -1; }
        ptr += err;
    }

    // Add hours, minutes, seconds, and milliseconds
    if (gl_time->tm_hour > 0 || gl_time->tm_min > 0) {
        err = snprintf(ptr, REMAIN_LEN, "%02d%02d", gl_time->tm_hour, gl_time->tm_min);
        if (err = 0) { return -1; }
        ptr += err;
    }

    return 0;
}

//�жϵ�ǰʱ�������ʼʱ���Ƿ񳬹�Ԥ������(��)
bool TIME_Timeing(time_t startTime, int64 maxSec) {
	if (getCurTick() - startTime < maxSec)
		return FALSE;
	return TRUE;
}

void mySleep(uint32 milliseconds) {
#ifdef WIN32
	Sleep(milliseconds);
#else
	struct timespec req;
	req.tv_sec = milliseconds / 1000;
	req.tv_nsec = (milliseconds % 1000) * 1000000;
	nanosleep(&req, NULL);
#endif // WIN32

}

void testTimeCounter() {
    uint64 start = myCounterStart();
    while (1) {
        printUsTick(myCounterUs(start)); printm("\n");
    }
    return;
}