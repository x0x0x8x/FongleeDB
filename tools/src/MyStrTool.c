#define _CRT_SECURE_NO_WARNINGS
#include "MyStrTool.h"
#include "MyTypes.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>



//将byte数值转换为xxGxx Mxx Kxx Byte的格式的字符串
void formatBytes(uint64 bytes, char* result) {
	unsigned long long gigabytes = bytes / (1024 * 1024 * 1024);
	unsigned long long megabytes = (bytes % (1024 * 1024 * 1024)) / (1024 * 1024);
	unsigned long long kilobytes = (bytes % (1024 * 1024)) / 1024;
	unsigned long long remainingBytes = bytes % 1024;

	// 格式化字符串
	strcpy(result, "");

	if (gigabytes > 0) {
		if(megabytes > 0 || kilobytes > 0 || remainingBytes > 0)
			sprintf(result + strlen(result), "%lluG ", gigabytes);
		else
			sprintf(result + strlen(result), "%lluG", gigabytes);
	}
	if (megabytes > 0) {
		if (kilobytes > 0 || remainingBytes > 0)
			sprintf(result + strlen(result), "%lluM ", megabytes);
		else
			sprintf(result + strlen(result), "%lluM", megabytes);
	}
	if (kilobytes > 0) {
		if (remainingBytes > 0)
			sprintf(result + strlen(result), "%lluK ", kilobytes);
		else
			sprintf(result + strlen(result), "%lluK", kilobytes);
	}
	if (remainingBytes > 0 || strlen(result) == 0) {
		sprintf(result + strlen(result), "%lluByte", remainingBytes);
	}
}
void printBytes(uint64 bytes) {
	char buff[64] = { 0 };
	formatBytes(bytes, buff);
	printf("%s", buff);
}
int strAppend(char* src, char* dest) {
	if (!src || !dest) {
		return -1;
	}
	memcpy(src+strlen(src), dest, strlen(dest));
	return 0;
}
uint32 strCountChar(char* str, uint32 len, char dest) {
	if (!str) { return 0; }
	uint32 ret = 0;
	for (uint32 i = 0; i < len;i++) {
		if (str[i] == dest) {
			ret++;
		}
	}
	return ret;
}