#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else

#endif
#include <stdlib.h>
#include <stdio.h>

// typedef
//------------------------------------
#ifdef WIN32
typedef char signed int8;
typedef char unsigned uint8;
typedef short signed int int16;
typedef short unsigned int uint16;
typedef long signed int int32;
typedef long unsigned int uint32;
typedef long long signed int int64;
typedef long long unsigned int uint64;
#else
typedef char signed int8;
typedef char unsigned uint8;
typedef short signed int int16;
typedef short unsigned int uint16;
typedef signed int int32;
typedef unsigned int uint32;
typedef long long signed int int64;
typedef long long unsigned int uint64;
#endif // WIN32

#ifndef bool
typedef unsigned char bool;
#endif // !bool

#ifndef TRUE
#define TRUE	((uint8)1)
#endif

#ifndef FALSE
#define FALSE	((uint8)0)
#endif

//------------------------------------
//define
//------------------------------------
# define U64_MAX	(0xffffffffffffffffULL)
# define U32_MAX	(0xffffffffULL)			//4G
# define U16_MAX	(0xffffULL)
# define U8_MAX		(0xffULL)

#define BYTE_1K ((uint64)1024)
#define BYTE_1M (BYTE_1K*1024)
#define BYTE_1G (BYTE_1M*1024)

#ifndef NULL
#define NULL	((void*)0)
#endif

# define CPU_CORE_NUM_MAX (8U)
# define PORT_PHYSICAL_NUM_MAX (20U)

# define ROUND_UP(a, b)	((((a) + (b) - 1) / (b)) * (b))			//5/3 = 6
# define CEIL_DIVIDE(a, b)	((a + b - 1) / b)					//5/3 = 1.2 = 2
# define MAX(a,b) ((a)>(b)?(a):(b))
# define MIN(a,b) ((a)<(b)?(a):(b))
# define BIT_SET(value,bit)	((value) |= (1LLU<<(bit)))
# define BIT_CLEAR(value,bit)	((value) &= ~(1LLU<<(bit)))
# define BIT_CHECK(value,bit)	(((value) & (1LLU<<(bit))) > 0 ? (TRUE):(FALSE))

#define MALLOC(type, buff, size) (while (buff == NULL) { buff = (type)malloc(size); }memset(buff, 0, size);)

// Mutex
typedef struct {
#ifdef _WIN32
	CRITICAL_SECTION mutex;
#else
	pthread_mutex_t mutex;
#endif
} Mutex;

