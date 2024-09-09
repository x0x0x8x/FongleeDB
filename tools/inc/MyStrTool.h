#include "MyTypes.h"

/*
字符串相关计算工具
*/

void formatBytes(uint64 bytes, char* result);
void printBytes(uint64 bytes);
int strAppend(char* src, char* dest);
uint32 strCountChar(char* str, uint32 len, char dest);