#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include "Hash.h"
#include "stdio.h"
#include "MyTool.h"
#include "MyStrTool.h"

static uint32 murmur3_32(const void* key, int len, uint32 seed) {
	/*
	整数输入
		输出范围在[0，128]之间，冲突率：约千分之7.8116	（70.3046875 / 9000）
		输出范围在[0，256]之间，冲突率：约千分之3.9058	（35.1523438 / 9000）
		输出范围在[0，512]之间，冲突率：约千分之1.9529	（17.57617 / 9000）
	*/
	if (key == NULL)
		return 0;
	const uint32 c1 = 0xcc9e2d51;
	const uint32 c2 = 0x1b873593;
	const uint32 r1 = 15;
	const uint32 r2 = 13;
	const uint32 m = 5;
	const uint32 n = 0xe6546b64;

	uint32 hash = seed;
	const int nblocks = len / 4;
	const uint32* blocks = (const uint32*)key;
	int i = 0;
	uint32 k = 0;

	for (i = 0;i<nblocks;i++) {
		k = blocks[i];
		k *= c1;
		k = (k << r1) | (k >> (32 - r1));
		k *= c2;

		hash ^= k;
		hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;
	}

	const uint8_t* tail = (const uint8_t*)key + (nblocks * 4);
	uint32 k1 = 0;
	switch (len&3) {
	case 3:
		k1 ^= tail[2] << 16;
	case 2:
		k1 ^= tail[1] << 8;
	case 1:
		k1 ^= tail[0];
		k1 *= c1;
		k1 = (k1 << r1) | (k >> (32 - r1));
		k1 *= c2;
		hash ^= k1;
	}

	hash ^= len;
	hash ^= (hash >> 16);
	hash *= 0x85ebca6b;
	hash ^= (hash >> 13);
	hash *= 0xc2b2ae35;
	hash ^= (hash >> 16);

	return hash;
}
static uint32 xxHash_32(const void* input, int len, uint32 seed) {
	/*
	整数输入
		输出范围在[0，128]之间，冲突率：约千分之7.8116	（70.30469 / 9000）
		输出范围在[0，256]之间，冲突率：约千分之3.9058	（35.15234 / 9000）
		输出范围在[0，512]之间，冲突率：约千分之1.9529	（17.57617 / 9000）
	*/
#define XXH_PRIME32_1 (0x9E3779B1U)
#define XXH_PRIME32_2 (0x85EBCA77U)
#define XXH_PRIME32_3 (0xC2B2AE3DU)
#define XXH_PRIME32_4 (0x27D4EB2FU)
#define XXH_PRIME32_5 (0x165667B1U)

	const uint8_t* p = (const uint8_t*)input;
	const uint8_t* end = p+len;
	uint32 h32;
	if (len >= 4) {
		const uint8* const limit = end - 4;
		uint32 v1 = seed + XXH_PRIME32_1 + XXH_PRIME32_2;
		do {
			v1 += (*(const uint32*)p) * XXH_PRIME32_2;
			v1 = (v1 << 13) | (v1 >> (32 - 13));
			v1 *= XXH_PRIME32_1;
			p += 4;
		} while (p <= limit);

		h32 = v1;
	}
	else {
		h32 = seed + XXH_PRIME32_5;
	}

	h32 += (uint32)len;

	while (p+4 <= end) {
		h32 += (*(const uint32*)p) + XXH_PRIME32_3;
		h32 = (h32 << 17) | (h32 >> (32 - 17));
		h32 *= XXH_PRIME32_4;
		p += 4;
	}

	while (p < end) {
		h32 += (*p) * XXH_PRIME32_5;
		h32 = (h32 << 11) | (h32 >> (32 - 11));
		h32 *= XXH_PRIME32_1;
		p++;
	}

	h32 ^= h32 >> 15;
	h32 *= XXH_PRIME32_2;
	h32 ^= h32 >> 13;
	h32 *= XXH_PRIME32_3;
	h32 ^= h32 >> 16;

	return h32;
}
static uint32 moduloHash_32(const int64 input, int len) {
	return input % len;
}
static uint32 multiplyHash(const int64 input, int len) {
	uint32 A = 2654435769;
	uint8 w = 32;

	return ((input * A) % (1llu << w)) >> (w - len);
}
static uint32 fnvHash(const char* input, uint32 len) {
	uint32 hash = 2166136261u;
	const uint32 prime = 16777619u;
	for (uint32 i = 0; i < len;i++) {
		hash ^= (uint32)input[i];
		hash *= prime;
	}
	return hash;
}
static uint32 sdbmHash(const char* input, uint32 len) {
	uint32 hash = 0;
	for (uint32 i = 0; i < len; i++) {
		hash = (uint8)(input[i]) + (hash << 6) + (hash << 16) - hash;
	}
	return hash;
}
static uint32 djb2Hash(const char* input, uint32 len) {
	uint32 hash = 5381;
	for (uint32 i = 0; i < len; i++) {
		hash = ((hash << 5) + hash) + ((uint8)(input[i]));
	}
	return hash;
}

static uint64 BloomFilterGetM(uint64 n, double p) {
	double m = -(n * log(p)) / (log(2)*log(2));
	return ceil(m);
}
static uint32 BloomFilterGetK(uint64 m, uint64 n) {
	return ceil((m / n) * log(2));
}

//p: 预期误报率 blockSize: 扇区字节数 diskSize: 磁盘总字节数
static uint32 BoolmFilterGetKByDiskInfo(uint64 diskSize,uint32 blockSize, double p, uint64* m) {
	uint64 n = diskSize / blockSize;
	*m = BloomFilterGetM(n, p);
	uint32 k = BloomFilterGetK(*m,n);
	return k;
}
//计算误报率 k: 哈希函数个数 m: 数组长度 n: 已存在元素个数
double BloomFilterCalculateP(uint64 k, uint64 m, uint64 n) {
	//k: hash func number
	//m: addr len
	//n: existed element number	

	double exponent = -((double)k * n) / m;
	double base = 1 - exp(exponent);
	return pow(base, k);
}

uint32 HashMurmur3_32(const void* key, int len, uint32 seed) {
	return murmur3_32(key, len, seed);
}
uint32 HashXXHash_32(const void* input, int len, uint32 seed) {
	return xxHash_32(input, len, seed);
}
uint32 HashModulo_32(int64 num, int len) {
	return moduloHash_32(num, len);
}
uint32 HashMultiply_32(int64 num, int len) {
	return multiplyHash(num, len);
}
uint32 HashFnvHash(const char* input, uint32 len) {
	return fnvHash(input, len);
}
uint32 HashSdbmHash(const char* input, uint32 len) {
	return sdbmHash(input, len);
}
uint32 HashDjb2Hash(const char* input, uint32 len) {
	return djb2Hash(input, len);
}

void hashTest() {

	/*
	扇区长度为512的话，1G磁盘有约2m个扇区
	*/
	uint64 diskSize = BYTE_1G*1024;//1t
	uint32 blockSize = 512U;
	
	for (uint32 i = 1; i <= 10;i++) {
		char buff[1024] = { 0 };
		formatBytes(diskSize * i, buff);
		printf("diskSize: [%s]\tp: 0.05\t", buff);
		uint64 m = 0;
		uint32 k = BoolmFilterGetKByDiskInfo(diskSize*i, blockSize, 0.05, &m);
		printm("k: %u\tm: ", k);
		memset(buff, 0, sizeof(buff));
		formatBytes((m / 8) * 3, buff);
		printm("%s\n", buff);
	}

	return;
}

uint64 HashBloomFilterGetM(uint64 n, double p) {
	return BloomFilterGetM(n, p);
}
uint32 HashBloomFilterGetK(uint64 m, uint64 n) {
	return BloomFilterGetK(m,n);
}
void testhashIntValue() {
	uint64 value = 1;
	uint32 len = 10240;
	uint32 hash = 0;
	uint32 hash1 = 0;

	for (uint32 i = 0; i < len/2; i++, value++) {
		/*
		整数输入
			输出范围在[0，512]之间，冲突率：约百分之35.937		（184 / 512）
			输出范围在[0，1024)之间，冲突率：约百分之36.035		（369 / 1024）
			输出范围在[0，4096]之间，冲突率：约百分之36.6699	（1502 / 4096）
			输出范围在[0，10240)之间，冲突率：约百分之36.5625	（3744 / 10240）

			元素数5120，桶数10240（2倍）, 冲突率：约百分之21.2695	(1089 / 5120)
		*/
		if (value == 9000) {
			printm("*************");
		}
		hash = xxHash_32(&value, sizeof(value), 0) % len;
		printm("%u\n", hash);
	}

	//for (uint32 i = 0; i < 1024;i++, len++) {
	//	value = 0;
	//	hash = murmur3_32(&value, sizeof(value), 0) % len;
	//	value = 1; 
	//	hash1 = murmur3_32(&value, sizeof(value), 0) % len;
	//	printm("%u, %u\n", hash, hash1);
	//}
	
}
