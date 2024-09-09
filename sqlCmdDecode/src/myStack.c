#include "myStack.h"
#include <malloc.h>
#include <stdio.h>

extern void stackShow(stackNode* stack);

uint32_t stackNodeCnt(stackNode* head) {
	uint32_t cnt = 0;
	stackNode* tmp = head;
	while (tmp != NULL) {
		tmp = tmp->next;
		cnt++;
	}
	return cnt;
}
uint32_t getStackNeedBufferSize(stackNode* head) {
	uint32_t size = 0;
	stackNode* ptr = head;
	while (ptr) {
		if (ptr->type.valueType != VT_Unkown) {
			size += (sizeof(stackNode) + ptr->size);// node + value
		}
		else {
			stackNode* sub = ptr->value;
			size += getStackNeedBufferSize(sub);
			size += sizeof(stackNode);
		}
		ptr = ptr->next;
	}

	return size;
}
int stack2Buffer(stackNode* head, char** buff, uint32_t* size, uint32_t* offset) {
	if (buff == NULL || size == NULL)
		return -1;
	int err = 0;

	uint32_t nodeCnt = stackNodeCnt(head);
	if (nodeCnt == 0)
		return -1;
	uint8_t isFirst = 0;
	if (*buff == NULL) {
		*size = getStackNeedBufferSize(head);
		*buff = malloc(*size); if (*buff == NULL) { return -2; }
		memset(*buff, 0, *size);
		isFirst = 1;
	}
	stackNode* tmp = head;
	char* ptr = *buff;
	while (tmp!=NULL) {
		if (tmp->type.valueType != VT_Unkown) {
			memcpy(ptr, tmp, sizeof(stackNode));	ptr += sizeof(stackNode);
			if (tmp->size > 0 && tmp->value) {
				memcpy(ptr, tmp->value, tmp->size);		ptr += tmp->size;
			}
		}
		else {
			memcpy(ptr, tmp, sizeof(stackNode));	ptr += sizeof(stackNode);
			uint32_t off = 0;
			err = stack2Buffer(tmp->value, &ptr, size, &off);
			if (err < 0) { 
				if (isFirst) { free(*buff); }
				return -1; 
			}
			ptr += off;
		}
		tmp = tmp->next;
	}

	if (offset)
		*offset = ptr - *buff;
	return 0;
}
void stackFreeStack(stackNode** head) {
	if (head == NULL)
		return;
	if (*head == NULL)
		return;
	stackNode* tmp = NULL;
	while (*head != NULL) {
		tmp = (*head)->next;
		free((*head)->value);
		free(*head);
		*head = tmp;
	}

	return;
}
void stackFreeNode(stackNode** node){
	if (node == NULL)
		return;
	if (*node == NULL)
		return;
	if ((*node)->next != NULL)
		return;
	free((*node)->value);
	free(*node);
	*node = NULL;

	return;
}
int stackPush(stackNode** stack, valueInfo type, char* value, uint32_t size) {
	if (stack == NULL)
		return -1;
	char* valueTmp = NULL;
	if (value && size > 0) {
		valueTmp = malloc(size);
		if (valueTmp == NULL)
			return -1;
		memset(valueTmp, 0, size);
	}
	
	if (*stack == NULL) {
		*stack = malloc(sizeof(stackNode));
		if (*stack == NULL) {
			free(valueTmp);
			return -1;
		}
		(*stack)->type = type;
		if(valueTmp && value)
			memcpy(valueTmp, value, size);
		(*stack)->value = valueTmp;
		(*stack)->size = size;
		(*stack)->next = NULL;
		return 0;
	}

	stackNode* tmp = *stack;
	while (tmp->next != NULL) {
		tmp = tmp->next;
	}
	tmp->next = malloc(sizeof(stackNode));
	
	if (tmp->next == NULL) {
		free(valueTmp);
		return -2;
	}
	memset(tmp->next, 0, sizeof(stackNode));

	tmp->next->type = type;
	tmp->next->value = valueTmp;
	if (valueTmp && value)
		memcpy(valueTmp, value, size);
	tmp->next->size = size;
	tmp->next->next = NULL;

	return 0;
}
stackNode* stackPop(stackNode** stack) {
	if (stack == NULL)
		return NULL;
	if (*stack == NULL)
		return NULL;
	stackNode* ret = NULL;
	if ((*stack)->next == NULL) {
		ret = (*stack);
		(*stack) = NULL;
		return ret;
	}

	stackNode* tmp = (*stack);
	while (tmp->next->next != NULL) {
		tmp = tmp->next;
	}
	ret = tmp->next;
	tmp->next = NULL;

	return ret;
}
int stackGetNodeIndex(stackNode* head, uint32_t cnt, stackNode* node, uint32_t* index) {
	if (head == NULL || node == NULL || index == NULL)
		return -1;
	stackNode* tmp = head;
	for (*index = 0; tmp != NULL && *index < cnt; (*index)++, tmp = tmp->next) {
		if (tmp == node)
			return 0;
	}

	return -1;
}
stackNode* stackGetTailNode(stackNode* stack, uint32_t cnt) {
	if (stack == NULL)
		return NULL;
	stackNode* tmp = stack;
	for (uint32_t i = 0; i < cnt-1 && tmp->next != NULL;i++) {
		tmp = tmp->next;
	}
	return tmp;
}
stackNode* stackGetNodeByIndex(stackNode* head, uint32_t index) {
	stackNode* tmp = head; 
	for (uint32_t i = 0; i <= index;i++, tmp=tmp->next) {
		if (tmp == NULL)
			return NULL;
		if (i == index) {
			return tmp;
		}
	}
	return NULL;
}
stackNode* stackGetLastNode(stackNode* head, stackNode* node) {
	if (head == NULL || node == NULL || head == node)
		return NULL;
	stackNode* tmp = head;
	while (tmp->next != NULL) {
		//stackShow(tmp);
		if (tmp->next == node)
			return tmp;
		tmp = tmp->next;
	}
	return NULL;
}
int stackRemoveNodeByIndex(stackNode** stack, uint32_t index) {
	if (stack == NULL)
		return -1;
	if (*stack == NULL)
		return -1;
	stackNode* tmp = NULL;
	if (index == 0) {
		tmp = (*stack)->next;
		stackFreeNode(stack);
		*stack = tmp;
		return 0;
	}
	tmp = *stack;
	for (uint32_t i = 0; i < index - 1;i++) {
		tmp = tmp->next;
		if (tmp == NULL)
			return -1;
	}
	if (tmp->next == NULL)
		return -1;
	stackNode* tmp2 = tmp->next->next;
	stackFreeNode(&(tmp->next));
	tmp->next = tmp2;

	return 0;
}
int stackRemoveNodeByPtr(stackNode** stack, stackNode* node) {
	if (stack == NULL || node == NULL)
		return -1;
	if (*stack == NULL)
		return -1;
	stackNode* tmp = NULL;
	if ((*stack) == node) {
		tmp = (*stack)->next;
		stackFreeNode(stack);
		*stack = tmp;
		return 0;
	}

	tmp = *stack;
	while (tmp->next != node) {
		tmp = tmp->next;
	}
	if (tmp->next != node)
		return -1;
	stackNode* tmp2 = tmp->next->next;
	stackFreeNode(&(tmp->next));
	tmp->next = tmp2;
	return 0;
}
int stackAppend(stackNode** head, stackNode* stack) {
	if (head == NULL || stack == NULL)
		return -1;
	if (*head == NULL) {
		*head = stack;
		return 0;
	}
	stackNode* tmp = *head;
	while (tmp->next != NULL) {
		tmp = tmp->next;
	}
	tmp->next = stack;
	return 0;
}
