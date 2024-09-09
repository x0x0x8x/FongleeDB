#include "BufferManager.h"

#define INVALID_BUFFER_LIST_NODE_INDEX (U32_MAX)


uint32 BuffListMgrGetListSize(BufferListInfo* head);
BufferListInfo* BuffListMgrGetTailNode(BufferListInfo* head, uint32 cnt);
uint32 BuffListMgrGetNodeIndex(BufferListInfo* head, uint32 cnt, BufferListInfo* node);
BufferListInfo* BuffListMgrGetNodeByIndex(BufferListInfo* head, uint32 index);
BufferListInfo* BuffListMgrGetLastNode(BufferListInfo* head, BufferListInfo* node);
int BuffListMgrRemoveNodeByIndex(BufferListInfo** head, uint32 index);
int BuffListMgrRemoveNodeByPtr(BufferListInfo** head, BufferListInfo* node);
int BuffGC(BufferPoolId poolId);//!!It must be ensured that no one is using the memory

void BufferManager_test1_byteAlignment();