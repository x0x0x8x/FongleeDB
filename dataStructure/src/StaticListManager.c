#include "StaticListManager.h"


#include <stdlib.h>
#include <string.h>

#ifdef DEBUG_STATIC_INDEX_LIST
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // !ASSERT

#define STATIC_INDEX_MAX (U16_MAX-1) //65535
#define INVALID_STATIC_INDEX (U16_MAX)

StaticIndexListNode gl_BufferListHeads[STATIC_LIST_BUFFER_POOL_MAX] = { 0 };//last == endNode, next == firstNode
StaticIndexListNode* gl_BufferListBuff = NULL;
uint32 gl_BufferListBuffCnt = 0;
uint32 gl_BufferListBuffRemainFreeNodeCnt = 0;

static void initStaticIndexList(StaticIndexListNode* head, uint16 cnt, uint16* headIndex, uint16* freeHeadIndex) {
	ASSERT(!head || !freeHeadIndex || !headIndex || cnt == 0, "invalid input");
	memset(head, 0, cnt * sizeof(StaticIndexListNode));
	head[0].last = INVALID_STATIC_INDEX;
	head[cnt - 1].next = INVALID_STATIC_INDEX;
	for (uint32 i = 0; i < cnt;i++) {
		head[i].next = i + 1;
	}
	*headIndex = INVALID_STATIC_INDEX;
	*freeHeadIndex = 0;
}
static int removeStaticIndex(StaticIndexListNode* head, uint16 destIndex, uint16* headIndex, uint16* freeHeadIndex) {
	ASSERT(!head || !freeHeadIndex || !headIndex || destIndex == INVALID_STATIC_INDEX, "invalid input");
	ASSERT(*headIndex == INVALID_STATIC_INDEX || *freeHeadIndex == INVALID_STATIC_INDEX, "invalid input");
	uint16 index = *headIndex;
	if (destIndex == *headIndex) {
		*headIndex = head[destIndex].next;
		head[destIndex].next = *freeHeadIndex;
		head[*freeHeadIndex].next = destIndex;
		*freeHeadIndex = destIndex;
		return 0;
	}
	while (index != INVALID_STATIC_INDEX) {
		if (head[index].next == destIndex) {
			head[index].next = head[destIndex].next;
			head[destIndex].next = *freeHeadIndex;
			head[*freeHeadIndex].next = destIndex;
			*freeHeadIndex = destIndex;
			return 0;
		}
	}
	ASSERT(TRUE, "removing index not in dest list");
	return -1;
}
static bool isInList(StaticListIndexType ListType, uint16 value) {
	StaticIndexListNode* buff = gl_BufferListBuff;
	uint16 index = gl_BufferListHeads[ListType].next;
	while (index != STATIC_LIST_INVALID_VALUE) {
		if (index == value) {
			return TRUE;
		}
		index = buff[index].next;
	}

	return FALSE;
}

void StaticListIndexInitSOW(uint64 indexCnt) {
	if (indexCnt == 0) {
		printm("init static list error 1\n");
		exit(0);
	}

	gl_BufferListBuff = malloc(indexCnt *sizeof(StaticIndexListNode));
	if (gl_BufferListBuff == NULL){
		printm("init static list error 3\n");
		exit(0);
	}
	gl_BufferListBuffCnt = (uint32)indexCnt;
	gl_BufferListBuffRemainFreeNodeCnt = gl_BufferListBuffCnt;
	memset(gl_BufferListBuff, U8_MAX, indexCnt * sizeof(StaticIndexListNode));
	memset(gl_BufferListHeads, U8_MAX, sizeof(StaticIndexListNode) * STATIC_LIST_BUFFER_POOL_MAX);

	return;
}
int StaticListIndexAddAfter(StaticListIndexType ListType, uint16 index, uint16 value) {
	StaticIndexListNode* buff = gl_BufferListBuff;
	ASSERT(index == STATIC_LIST_INVALID_VALUE || value == STATIC_LIST_INVALID_VALUE, "StaticListIndexAddAfter invalid input\n");
	ASSERT(buff[value].last != STATIC_LIST_INVALID_VALUE || buff[value].next != STATIC_LIST_INVALID_VALUE, "StaticListIndexAddAfter not free node\n");

	if (gl_BufferListBuffRemainFreeNodeCnt == 0) {
		return -2;
	}
	
	if (gl_BufferListHeads[ListType].last == index) {
		//end node
		buff[value].last = index;
		buff[value].next = STATIC_LIST_INVALID_VALUE;
		buff[index].next = value;
		gl_BufferListHeads[ListType].last = value;
	}
	else {
		uint16 next = buff[index].next;
		//next not null, because index is not end node
		buff[value].last = index;
		buff[value].next = next;
		buff[index].next = value;
		buff[next].last = value;
	}
	
	gl_BufferListBuffRemainFreeNodeCnt--;
	return 0;
}
int StaticListIndexAddBefore(StaticListIndexType ListType, uint16 index, uint16 value) {
	StaticIndexListNode* buff = gl_BufferListBuff;
	ASSERT(index == STATIC_LIST_INVALID_VALUE || value == STATIC_LIST_INVALID_VALUE, "StaticListIndexAddBefore invalid input\n");
	ASSERT(buff[value].last != STATIC_LIST_INVALID_VALUE || buff[value].next != STATIC_LIST_INVALID_VALUE, "StaticListIndexAddBefore invalid input, not free node\n");
	if (gl_BufferListBuffRemainFreeNodeCnt == 0) {
		return -2;
	}
	if (gl_BufferListHeads[ListType].next == index) {
		//first node
		uint16 tail = gl_BufferListHeads[ListType].last;
		uint16 head = gl_BufferListHeads[ListType].next;

		gl_BufferListHeads[ListType].next = value;
		buff[head].last = value;
		buff[value].next = head;
	}
	else {
		uint16 last = buff[index].last;

		buff[last].next = value;
		buff[index].last = value;
		buff[value].last = last;
		buff[value].next = index;
	}
	
	gl_BufferListBuffRemainFreeNodeCnt--;
	return 0;
}
int StaticListIndexRemove(StaticListIndexType ListType, uint16 value) {
	StaticIndexListNode* buff = gl_BufferListBuff;
	ASSERT(!isInList(ListType, value), "StaticListIndexAddBefore invalid input, not free node\n");
	if (gl_BufferListHeads[ListType].next == value) {
		//first
		uint16 last = buff[value].last;
		uint16 next = buff[value].next;
		if (next != STATIC_LIST_INVALID_VALUE) {
			buff[next].last = last;
		}
		gl_BufferListHeads[ListType].next = next;
		if (next == STATIC_LIST_INVALID_VALUE) {
			gl_BufferListHeads[ListType].last = STATIC_LIST_INVALID_VALUE;
		}
	}
	else if(gl_BufferListHeads[ListType].last == value){
		//end (not only one)
		uint16 last = buff[value].last;
		uint16 next = buff[value].next;
		if (last != STATIC_LIST_INVALID_VALUE) {
			buff[last].next = next;
		}
		ASSERT(last == STATIC_LIST_INVALID_VALUE, "StaticListIndexRemove last is null?\n");
		gl_BufferListHeads[ListType].last = last;
	}
	else {
		uint16 last = buff[value].last;
		uint16 next = buff[value].next;
		
		buff[last].next = next;
		buff[next].last = last;
	}

	buff[value].last = STATIC_LIST_INVALID_VALUE;
	buff[value].next = STATIC_LIST_INVALID_VALUE;

	gl_BufferListBuffRemainFreeNodeCnt++;
	return 0;
}
int StaticListIndexAppend(StaticListIndexType ListType, uint16 value) {
	StaticIndexListNode* buff = gl_BufferListBuff;
	ASSERT(buff[value].last != STATIC_LIST_INVALID_VALUE || buff[value].next != STATIC_LIST_INVALID_VALUE, "StaticListIndexAppend not free node\n");

	if (gl_BufferListHeads[ListType].next == STATIC_LIST_INVALID_VALUE) {
		//¿ÕÁÐ±í
		gl_BufferListHeads[ListType].last = value;
		gl_BufferListHeads[ListType].next = value;
	}
	else {
		uint16 tail = gl_BufferListHeads[ListType].last;
		uint16 head = gl_BufferListHeads[ListType].next;

		buff[value].last = tail;
		buff[tail].next = value;
		gl_BufferListHeads[ListType].last = value;
	}
	
	gl_BufferListBuffRemainFreeNodeCnt--;
	return 0;
}
uint16 StaticListIndexGetHeadByListType(StaticListIndexType ListType) {
	return gl_BufferListHeads[ListType].next;
}
uint16 StaticListIndexGetNextNode(StaticListIndexType ListType, uint16 value) {
	StaticIndexListNode* buff = gl_BufferListBuff;
	ASSERT(value == STATIC_LIST_INVALID_VALUE, "StaticListIndexGetNextNode invalid node\n");
	return buff[value].next;
}

bool DEBUG_FindValueInList(uint16 value) {
	uint16 index = STATIC_LIST_INVALID_VALUE;
	for (uint32 i = 0; i < STATIC_LIST_BUFFER_POOL_MAX;i++) {
		for (index = StaticListIndexGetHeadByListType(i); index != STATIC_LIST_INVALID_VALUE; index = StaticListIndexGetNextNode(i, index)) {
			if (index == value) {
				DEBUG_StaticListIndexViewLists();
				return TRUE;
			}
		}
	}
	
	return FALSE;
}
void DEBUG_StaticListIndexViewLists() {
	StaticIndexListNode* buff = gl_BufferListBuff;
	uint32 validListCnt = STATIC_LIST_BUFFER_POOL_MAX;
	for (int i = 0;i< STATIC_LIST_BUFFER_POOL_MAX;i++) {
		if (gl_BufferListHeads[i].last != U16_MAX) {
			uint16 index = gl_BufferListHeads[i].next;
			printm("List(%d): (%d)[%d](%d) ", i,buff[gl_BufferListHeads[i].next].last, gl_BufferListHeads[i].next, buff[gl_BufferListHeads[i].next].next);
			while (buff[index].next != U16_MAX) {
				printm("(%d)[%d](%d) ",buff[buff[index].next].last, buff[index].next, buff[buff[index].next].next);
				index = buff[index].next;
			}
			printm("\n");
		}
		else {
			validListCnt--;
		}
	}
	if (validListCnt == 0) {
		printm("All list empty\n");
	}
}
void DEBUG_StaticListIndexTestCase() {
	int err = 0;

	StaticListIndexInitSOW(1024);
	StaticListIndexType ListType = STATIC_LIST_BUFFER_POOL0;
	StaticListIndexAppend(ListType, 0);			DEBUG_StaticListIndexViewLists();
	StaticListIndexAppend(ListType, 1);			DEBUG_StaticListIndexViewLists();
	StaticListIndexAppend(ListType, 2);			DEBUG_StaticListIndexViewLists();
											
	StaticListIndexRemove(ListType, 1);			DEBUG_StaticListIndexViewLists();
	StaticListIndexAppend(ListType, 1);			DEBUG_StaticListIndexViewLists();
											
	StaticListIndexRemove(ListType, 1);			DEBUG_StaticListIndexViewLists();
	StaticListIndexAppend(ListType, 1);			DEBUG_StaticListIndexViewLists();
											
	StaticListIndexRemove(ListType, 0);			DEBUG_StaticListIndexViewLists();
	StaticListIndexAppend(ListType, 0);			DEBUG_StaticListIndexViewLists();


	StaticListIndexAddAfter(ListType, 2, 3);		DEBUG_StaticListIndexViewLists();
	StaticListIndexAddAfter(ListType, 0, 4);		DEBUG_StaticListIndexViewLists();


	StaticListIndexAddBefore(ListType, 4, 5);	DEBUG_StaticListIndexViewLists();
	StaticListIndexAddBefore(ListType, 2, 6);	DEBUG_StaticListIndexViewLists();

	err = StaticListIndexAppend(ListType, 0);	DEBUG_StaticListIndexViewLists();
}