#pragma once
#include "MyTypes.h"

#define DEBUG_ON
#ifdef DEBUG_ON
#define ASSERT_GL(condition,...) assert_gl(condition,__VA_ARGS__);
#else
#define ASSERT_GL(condition,...);
#endif // DEBUG_ON

int strAppend(char* str1, const char* str2);

bool isSmallEndian();
void printBit(int p);

int getSystemErrCode();
uint32 getU32RandomNumber();

//base59
int baseConversion(uint64 base10Num, uint32 destBase, char* out, uint64 outlen);
int base10ToBase59(uint64 base10Num, char* base59, uint64 outlen);

//bit array handle
void setBitArrayByIndex(char* arr, uint64 index);
bool checkBitArrayByIndex(char* arr, uint64 index);
void cleanBitArrayByIndex(char* arr, uint64 index);
uint64 getBitArrayMaxIndex(char* arr, uint64 maxBit);

//»¥³âËø
void initMutex(Mutex* mutex);
void lockMutex(Mutex* mutex);
void unlockMutex(Mutex* mutex);
int tryLockMutex(Mutex* mutex);
void removeMutex(Mutex* mutex);

//·â×°º¯Êý
void printm(const char* format, ...);
void assert_gl(bool condition, const char* format, ...);
void keybordInput(char* buff, uint32 len);
void print_progress_bar(int percentage);//percentage:0~100
void hide_cursor();
void show_cursor();