#include "BufferListManager.h"

static uint32 getListSize(BufferListInfo* head) {
	uint32 cnt = 0;
	BufferListInfo* tmp = head;
	
	while (tmp) {
		cnt++;
		tmp = tmp->next;
	}

	return cnt;
}
static BufferListInfo* getTailNode(BufferListInfo* head, uint32 cnt) {
	BufferListInfo* tmp = head;
	if (!tmp) { return NULL; }
	for (uint32 i = 0; i < cnt && tmp;i++, tmp = tmp->next) {
		if (i == cnt - 1) {
			return tmp;
		}
	}
	//never hear
	return NULL;
}
static uint32 getNodeIndex(BufferListInfo* head, uint32 cnt, BufferListInfo* node) {
	uint32 index = 0;
	BufferListInfo* tmp = head;
	for (index = 0; tmp != NULL && index < cnt; index++, tmp = tmp->next) {
		if (tmp == node)
			return index;
	}
	return INVALID_BUFFER_LIST_NODE_INDEX;
}
static BufferListInfo* getNodeByIndex(BufferListInfo* head, uint32 index) {
	BufferListInfo* tmp = head;
	uint32 i = 0;
	while (tmp) {
		if (i == index) {
			return tmp;
		}
		tmp = tmp->next;
		i++;
	}
	return NULL;
}
static BufferListInfo* getLastNode(BufferListInfo* head, BufferListInfo* node) {
	if (!node) {
		return NULL;
	}
	BufferListInfo* last = head;
	while (last) {
		if(last->next == node){
			return last;
		}
		last = last->next;
	}
	return NULL;
}
static int removeNodeByIndex(BufferListInfo** head, uint32 index) {
	uint32 i = 0;
	if (index == 0) {
		BufferListInfo* tmp = (*head)->next;
		BuffRelease(head);
		*head = tmp;
		return 0;
	}
	BufferListInfo* tmp = (*head);
	while (1) {
		if (index - 1 == i) {
			BufferListInfo* t2 = tmp->next->next;
			BuffRelease(&tmp->next);
			tmp->next = t2;
			return 0;
		}
		tmp = tmp->next;
	}
	//never hear
	return -1;
}
static int removeNoedByPtr(BufferListInfo** head, BufferListInfo* node) {
	BufferListInfo* tmp = *head;
	if (*head == node) {
		tmp = (*head)->next;
		BuffRelease(head);
		(*head) = tmp;
		return 0;
	}
	while (tmp) {
		if (tmp->next == node) {
			BufferListInfo* t2 = tmp->next->next;
			BuffRelease(&tmp->next);
			tmp->next = t2;
			return 0;
		}
		tmp = tmp->next;
	}
	//never hear
	return -1;
}

uint32 BuffListMgrGetListSize(BufferListInfo* head) {
	return getListSize(head);
}
BufferListInfo* BuffListMgrGetTailNode(BufferListInfo* head, uint32 cnt) {
	return getTailNode(head, cnt);
}
uint32 BuffListMgrGetNodeIndex(BufferListInfo* head, uint32 cnt, BufferListInfo* node) {
	return getNodeIndex(head, cnt, node);
}
BufferListInfo* BuffListMgrGetNodeByIndex(BufferListInfo* head, uint32 index) {
	return getNodeByIndex(head, index);
}
BufferListInfo* BuffListMgrGetLastNode(BufferListInfo* head, BufferListInfo* node) {
	return getLastNode(head, node);
}
int BuffListMgrRemoveNodeByIndex(BufferListInfo** head, uint32 index) {
	return removeNodeByIndex(head, index);
}
int BuffListMgrRemoveNodeByPtr(BufferListInfo** head, BufferListInfo* node) {
	return removeNoedByPtr(head, node);
}