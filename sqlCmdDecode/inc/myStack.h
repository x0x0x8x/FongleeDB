#pragma once
#include <stdint.h>
#include "operatorTypeDef.h"

typedef struct stackNode_t{
	valueInfo type;//valueInfo
	uint32_t size;
	char* value;
	struct stackNode_t* next;
}stackNode;

int stack2Buffer(stackNode* head, char** buff, uint32_t* size, uint32_t* offset);//stack转成连续buffer

uint32_t stackNodeCnt(stackNode* head);
void stackFreeStack(stackNode** head);
void stackFreeNode(stackNode** node);
int stackPush(stackNode** stack, valueInfo type, char* value, uint32_t size);
stackNode* stackPop(stackNode** stack);
int stackAppend(stackNode** head, stackNode* stack);

stackNode* stackGetTailNode(stackNode* stack, uint32_t cnt);
stackNode* stackGetLastNode(stackNode* head, stackNode* node);
int stackGetNodeIndex(stackNode* head, uint32_t cnt, stackNode* node, uint32_t* index);

int stackRemoveNodeByIndex(stackNode** stack, uint32_t index);
int stackRemoveNodeByPtr(stackNode** stack, stackNode* node);
stackNode* stackGetNodeByIndex(stackNode* head, uint32_t index);