#pragma once
#include "MyTypes.h"
#include "MyTool.h"

#define DEBUG_STATIC_INDEX_LIST
#define STATIC_LIST_DEBUG

#define STATIC_LIST_INVALID_VALUE (U16_MAX)

typedef struct StaticIndexListNode_t {
	uint16 last;
	uint16 next;
	//uint8 isInValid;
}StaticIndexListNode;


//---Ã¶¾Ù
typedef enum StaticListIndexType_e {
	STATIC_LIST_BUFFER_POOL0,
	STATIC_LIST_BUFFER_POOL1,
	STATIC_LIST_BUFFER_POOL2,
	STATIC_LIST_BUFFER_POOL3,
	//STATIC_LIST_BUFFER_POOL4,
	//STATIC_LIST_BUFFER_POOL5,
	//STATIC_LIST_BUFFER_POOL6,
	//STATIC_LIST_BUFFER_POOL7,
	//STATIC_LIST_BUFFER_POOL8,
	//STATIC_LIST_BUFFER_POOL9,
	STATIC_LIST_BUFFER_POOL_MAX
}StaticListIndexType;

void StaticListIndexInitSOW(uint64 indexCnt);
int StaticListIndexRemove(StaticListIndexType ListType, uint16 value);
int StaticListIndexAddAfter(StaticListIndexType ListType, uint16 index, uint16 value);
int StaticListIndexAddBefore(StaticListIndexType ListType, uint16 index, uint16 value);
int StaticListIndexAppend(StaticListIndexType ListType, uint16 value);
uint16 StaticListIndexGetHeadByListType(StaticListIndexType ListType);
int StaticListIndexSetInvalidButNoRelease(StaticListIndexType ListType, uint16 value);
uint16 StaticListIndexGetNextNode(StaticListIndexType ListType, uint16 value);
bool StaticListIndexIsInvalid(StaticListIndexType ListType, uint16 value);


void DEBUG_StaticListIndexViewLists();
bool DEBUG_FindValueInList(uint16 value);
void DEBUG_StaticListIndexTestCase();