#pragma once
#include "MyTypes.h"

typedef enum HashFunType_e {
    HASH_MURMUR3_32,
    HASH_XXHASH_32,
    HASH_MODULO_32,
    HASH_MULTIPLY_32,
}HashFunType;

uint64 HashBloomFilterGetM(uint64 n, double p);
uint32 HashBloomFilterGetK(uint64 m, uint64 n);
double BloomFilterCalculateP(uint64 k, uint64 m, uint64 n);//计算误报率 k: 哈希函数个数 m: 数组长度 n: 已存在元素个数

uint32 HashMurmur3_32(const void* key, int len, uint32 seed);
uint32 HashXXHash_32(const void* input, int len, uint32 seed);
uint32 HashModulo_32(int64 num, int len);//取模散列
uint32 HashMultiply_32(int64 num, int len);//乘法散列
uint32 HashFnvHash(const char* input, uint32 len);
uint32 HashSdbmHash(const char* input, uint32 len);
uint32 HashDjb2Hash(const char* input, uint32 len);

void hashTest();
void testhashIntValue();