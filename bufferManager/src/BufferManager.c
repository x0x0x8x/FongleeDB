#include "stdlib.h"
#include "BufferManager.h"
#include "StaticListManager.h"
#include <string.h>
#include "TaskHandler.h"
#include "MyTool.h"
#include "MyTime.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_BUFFER_MANAGER
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_TASK_HANDLER

#define ERROR(errCode) {err = errCode;goto error;}

#define BUFFER_LIST_CNT_MAX (U16_MAX-1) //65535
#define INVALID_BUFFER_LIST_NODE_INDEX (U16_MAX)


char* BufferPoolStr[BUFFER_POOL_MAX] = {
	"Normal",
};

extern StaticIndexListNode* gl_BufferListBuff;
extern StaticIndexListNode gl_BufferListHeads[STATIC_LIST_BUFFER_POOL_MAX];
static char* gl_realBuffHead = NULL;
static char* gl_buff = NULL;
static uint64 gl_size = 0;
static uint64 gl_remainSize = 0;
//---pool
static BufferPool gl_poolArr[BUFFER_POOL_MAX] = {0};
static uint32 gl_poolCnt = 0;
static StaticListIndexType gl_lastListType = STATIC_LIST_BUFFER_POOL0;
//------

//---buffer list
static uint32 gl_bufferListIndexListCnt = 0;
static uint32 gl_bufferListIndexListFreeHead = 0;

static BufferListInfo* gl_bufferList = NULL;//存放可被用户获取的buffer对象
static uint32 gl_bufferListSize = 0;//BufferListInfo的数量 不超过u16_max
static uint32 gl_remainBufferListCnt = 0;//剩余可用的BufferListInfo的数量
//-----------

static BufferManagerBitMap gl_BitMap = { 0 };
static Mutex gl_mutex;

BufferPool* getPoolById(BufferPoolId id);

char* getNewBuffer(uint64 size) {
	if (gl_realBuffHead != NULL) {
		free(gl_realBuffHead);
		gl_buff = NULL;
	}
	gl_realBuffHead = malloc(size + 7);
	if (((uint64)(gl_realBuffHead)) % 8 > 7) {
		printm("ERROR getNewBuffer\n");
		exit(0);
	}
	if (gl_realBuffHead != NULL) {
		if (((uint64)(gl_realBuffHead)) % 8 != 0) {
			gl_buff = gl_realBuffHead + ((uint64)(gl_realBuffHead)) % 8;
		}
		else {
			gl_buff = gl_realBuffHead;
		}
	}
	else {
		return NULL;
	}

	gl_size = size;
	return gl_buff;
}
BufferListInfo* getNewBufferList(uint64 size) {
	gl_bufferListSize = size / BUFFER_LIST_WEIGHT;
	if(gl_bufferListSize < BUFFER_LIST_MIN_SIZE)
		gl_bufferListSize = BUFFER_LIST_MIN_SIZE;
	if (gl_bufferList != NULL)
		free(gl_bufferList);
	gl_bufferList = (BufferListInfo*)malloc(gl_bufferListSize);
	
	if (gl_bufferList != NULL) {
		gl_bufferListSize /= sizeof(BufferListInfo);
		StaticListIndexInitSOW(gl_bufferListSize);
		printm("BufferList cnt = %d\n", gl_bufferListSize);
	}
	else
		gl_bufferListSize = 0;


	return gl_bufferList;
}
bool isInvalidBufferList(BufferListInfo* bufferList) {
	return (bufferList < gl_bufferList || bufferList >= gl_bufferList + gl_bufferListSize) && bufferList != NULL;
}
BufferPool* getPoolById(BufferPoolId id) {
	if (id >= BUFFER_POOL_MAX) {
		return NULL;
	}
	return gl_poolArr + id;
}

static bool isRuningGC(BufferPoolId poolId) {
	return getPoolById(poolId)->Bitmap.isGC;
}
int addNewPool(BufferPoolId id, uint32 weight) {
	if (gl_BitMap.Inited || gl_lastListType >= STATIC_LIST_BUFFER_POOL_MAX)
		return -2;

	gl_poolArr[id].weight = weight;
	gl_poolArr[id].listType = gl_lastListType++;
	gl_poolCnt++;

	return 0;
}
int addNewPoolByConstSize(BufferPoolId id, uint64 size) {
	gl_poolArr[id].size = size;
	gl_poolArr[id].remainSize = size;
	gl_poolArr[id].listType = gl_lastListType++;
	gl_poolCnt++;
}
int initBuffPoolByConstSize() {
	int err = 0;
	uint64 maxSize = 0;
	for (uint32 i = 0; i < BUFFER_POOL_MAX;i++) {
		maxSize += gl_poolArr[i].size;
	}
	gl_bufferList = getNewBufferList(maxSize);
	if (gl_bufferList == NULL)
		return -1;
	gl_remainBufferListCnt = gl_bufferListSize;
	memset(gl_bufferList, 0, gl_bufferListSize * sizeof(BufferListInfo));

	gl_buff = getNewBuffer(maxSize);
	if (gl_buff == NULL)
		return -1;

	gl_remainSize = maxSize;
	memset(gl_buff, U8_MAX, maxSize);

	char* buff = gl_buff;
	printm("***Buffer pool init***\n");

	uint32 j = 0;
	for (uint32 i = 0; i < BUFFER_POOL_MAX; i++) {
		if (j < gl_poolCnt - 1) {
			gl_poolArr[i].buffHead = buff;
			gl_poolArr[i].weight = gl_poolArr[i].size / maxSize;
			buff += gl_poolArr[i].size;
		}
		else {
			gl_poolArr[i].buffHead = buff;
			gl_poolArr[i].weight = gl_poolArr[i].size / maxSize;
		}
		initMutex(&(gl_poolArr[i].mutex));
		printm("init buffer pool mutex[%lu]\n", i);

		j++;
		#ifdef DEBUG_BUFFER_MANAGER

		char test[100] = { 0 };
		formatBytes(gl_poolArr[i].size, test);
		printm("%s pool, size[%s]\n", BufferPoolStr[i], test);

		#endif // DEBUG_BUFFER_MANAGER
	}
	printm("***************\n");

	gl_BitMap.Inited = TRUE;
	initMutex(&gl_mutex);
	return 0;
}
int initBufferManagserBySize(uint32 poolCnt, ...) {
	int err = 0;
	va_list args;
	va_start(args, poolCnt);
	for (uint32 i = 0; i < poolCnt; i++) {
		BufferPoolId id = va_arg(args, BufferPoolId);
		uint64 size = va_arg(args, uint32);
		err = addNewPoolByConstSize(id, size);
		ASSERT(err < 0, "add buffer pool fail");
		if (err < 0) { return -1; }
	}
	va_end(args);
	err = initBuffPoolByConstSize();
	ASSERT(err < 0, "init buffer pool fail");
	if (err < 0) { return -1; }

	return 0;
}
int creatPoolSize(BufferPoolId destPoolId, BufferPoolId srcPoolId, uint64 size) {

	return 0;
}

//消除公约数
void resetPoolWeightGCD() {
	uint32 minWeight = U32_MAX;
	int i = 0;
	bool isGCD = TRUE;

	for (i = 0;i<gl_poolCnt;i++) {
		if (gl_poolArr[i].weight < minWeight)
			minWeight = gl_poolArr[i].weight;
	}
	for (i = 0; i < gl_poolCnt;i++) {
		if (gl_poolArr[i].weight == minWeight)
			continue;
		if (gl_poolArr[i].weight % minWeight != 0) {
			isGCD = FALSE;
			break;
		}
	}
	if (isGCD) {
		for (i = 0; i < gl_poolCnt; i++) {
			gl_poolArr[i].weight /= minWeight;
		}
	}

	return;
}
int defrageBuffInPool(BufferPoolId poolId) {
	int err = 0;
	BufferPool* curPool = getPoolById(poolId);
	if (curPool == NULL) {
		printm("error: invalid pool id\n");
		ERROR(-1);
	}
	curPool->Bitmap.isGC = TRUE;
	//wait buffer not be read in is pool
	time_t start = getCurTick();
	while (curPool->Bitmap.lock) {
		if (TIME_Timeing(start, 60)) {
			printm("buffer GC wait lock timeout 60s\n");
			ERROR(-2);
		}
	}

	printm(">>>pool defrage\n");
	char* startPtr = curPool->buffHead;
	char* endPtr = curPool->buffHead + curPool->size;

	uint16 index = StaticListIndexGetHeadByListType(curPool->listType);
	char* lastBuff = curPool->buffHead;
	for (; index != U16_MAX;index = StaticListIndexGetNextNode(curPool->listType, index)) {
		memmove(lastBuff, gl_bufferList[index].buff, gl_bufferList[index].size);
		gl_bufferList[index].buff = lastBuff;
		lastBuff += gl_bufferList[index].size;
	}
#ifdef DEBUG_BUFFER_MANAGER
	memset(lastBuff, U8_MAX, endPtr - lastBuff);
#endif // DEBUG_BUFFER_MANAGER
	curPool->Bitmap.isGC = FALSE;

	return 0;
error:
	curPool->Bitmap.isGC = FALSE;
	return err;
}
int getNextValidBufferList(BufferListInfo** out, uint16* outIndex) {
#ifdef DEBUG_BUFFER_MANAGER
	if (out == NULL || outIndex == NULL) {
		return -1;
	}
#endif // DEBUG_BUFFER_MANAGER

	*out = gl_bufferList;
	*outIndex = 0;

	if (gl_remainBufferListCnt == 0) {
		printm("buffer list 不足\n");
		return -3;
	}
	
	while ((*out)->userNum > 0) {
		(*out)++;
		(*outIndex)++;
		ASSERT((*outIndex) == U16_MAX, "out of static list index(u16) range\n");
	}
	memset((*out), 0, sizeof(BufferListInfo));
	
	return 0;
}
int BuffFirstFit(BufferListInfo* bufferList, uint64 size, BufferPoolId poolId) {
	int err = 0;
	ASSERT(bufferList == NULL || size == 0,"BuffFirstFit error: invalid input param\n");
	BufferPool* pool = getPoolById(poolId);
	char* freeBuffEnd = pool->buffHead + pool->size;
	if (pool->remainSize < size) {
		printm("BuffFirstFit error: no any more buffer in pool[%d]", poolId);
		return -2;
	}
	uint16 index = StaticListIndexGetHeadByListType(pool->listType);
	uint16 nextIndex = STATIC_LIST_INVALID_VALUE;

	//set bufferList->buff
	if (index == STATIC_LIST_INVALID_VALUE) {
		//no any buffer list
		bufferList->buff = pool->buffHead;
		uint16 curIndex = bufferList - gl_bufferList;
		err = StaticListIndexAppend(pool->listType, curIndex); if (err) { return -3; }
	}
	else {
		if (gl_bufferList[index].buff - pool->buffHead >= size) {
			//第一段buffer之前有足够空间
			uint16 curIndex = bufferList - gl_bufferList;
			err = StaticListIndexAddBefore(pool->listType, index, curIndex); if (err) { return -3; }
			bufferList->buff = pool->buffHead;
		}
		else {
			for (;; index = nextIndex) {
				nextIndex = StaticListIndexGetNextNode(pool->listType, index);
				if (nextIndex != STATIC_LIST_INVALID_VALUE) {
					char* lastTail = ROUND_UP((uint64)(gl_bufferList[index].buff + gl_bufferList[index].size), 8);//ROUND_UP((uint64)(gl_bufferList[index].buff + gl_bufferList[index].size), 8)
					if (gl_bufferList[nextIndex].buff - lastTail >= size) {
						//当前buffer到下一个buffer之间的连续内存大于等于待申请内存，直接分配
						bufferList->buff = lastTail;
						err = StaticListIndexAddAfter(pool->listType, index, bufferList - gl_bufferList); if (err) { return -2; }
						break;
					}
					else {
						continue;
					}
				}
				else {
					char* freeBuffStart = ROUND_UP((uint64)(gl_bufferList[index].buff + gl_bufferList[index].size), 8);
					if (freeBuffEnd - freeBuffStart < (size / 8)*8) {
						return -2;//remain continues buffer not enough
						//err = defrageBuffInPool(poolId);
						//if (err > 0) {
						//	return 1;//该pool已锁定, 如正在并发读写
						//}
						//else if (err < 0) {
						//	return -3;
						//}
						//freeBuffStart = ROUND_UP((uint64)(gl_bufferList[index].buff + gl_bufferList[index].size), 8);
					}
					if (freeBuffEnd - freeBuffStart < (size / 8) * 8) { 
						return -4; //remain size enough, but 8byte alignment's size not enough
					}
					bufferList->buff = freeBuffStart;
					StaticListIndexAppend(pool->listType, bufferList - gl_bufferList);
					break;
				}
			}

		}
		
	}

	bufferList->size = size;
	bufferList->poolId = poolId;
	bufferList->userNum = 1;
	
	return 0;
}
int BuffRealloc(BufferListInfo** lastBuff, uint64 destSize, BufferPoolId poolId) {
	int err = 0;
	if (lastBuff == NULL) {
		return -1;
	}
	BufferListInfo* tmp = NULL;
	err = BuffRequest(destSize, poolId, &tmp);
	if (err != 0) {
		printm("BuffRealloc error : 内存不足\n");
		return -2;
	}
	memset(tmp->buff, 0, tmp->size);
	memcpy(tmp->buff, (*lastBuff)->buff, MIN((*lastBuff)->size, tmp->size));
	tmp->userNum = (*lastBuff)->userNum;

	BuffRelease(lastBuff);
	(*lastBuff) = tmp;

	return 0;
}
BufferListInfo* getBuffer(uint64 size, BufferPoolId poolId) {
	BufferListInfo* ret =  NULL;
	int err = BuffRequest(size, poolId, &ret);
	return ret;
}
void freeBuffer(BufferListInfo** buffList) {
	BuffRelease(buffList);
}
//return-> 1: 需等待重试
int BuffRequest(uint64 size, BufferPoolId poolId, BufferListInfo** out) {
	int err = 0;
	ASSERT(gl_BitMap.Inited != TRUE, "BufferManager 未初始化\n");
	lockMutex(&gl_mutex);
	if (*out != NULL) {
		unlockMutex(&gl_mutex);
		return -1;
	}
	if (gl_BitMap.Sleeping) {
		unlockMutex(&gl_mutex);
		return -1;
	}
	
	BufferPool* curPool = getPoolById(poolId);
	if (curPool == NULL) {	//不存在的pool id
		printm("BuffRequest: invalid poolId\n");
		unlockMutex(&gl_mutex);
		return -2;
	}
	
	if (curPool->remainSize < size) {
		printm("remain pool size not enough\n");
		unlockMutex(&gl_mutex);
		return -4;
	}
	BufferListInfo* bufferListInfo = NULL;
	uint16 index = U16_MAX;
	err = getNextValidBufferList(&bufferListInfo, &index);
	if (isInvalidBufferList(bufferListInfo)) {
		ASSERT(TRUE, "invalid buffer!!!\n");
		exit(0);
	}
	if (err < 0) {
		unlockMutex(&gl_mutex);
		return -5;
	}
	
	err = BuffFirstFit(bufferListInfo, size, poolId);
	if (err < 0) {
		unlockMutex(&gl_mutex);
		return -6;
	}
	else if(err > 0){
		//pool已锁定，无法内存整理，需要稍后重试
		unlockMutex(&gl_mutex);
		return 1;
	}

	gl_remainBufferListCnt--;
	gl_remainSize -= size;
	curPool->remainSize -= size;
	
#ifdef DEBUG_BUFFER_MANAGER
	char tmp[64] = { 0 };
	formatBytes(curPool->remainSize, tmp);
	printm("pool[%d] remain free size: %s, req[%lldByte]\n", poolId, tmp, size);
#endif // DEBUG_BUFFER_MANAGER
#ifdef DEBUG_BUFFER_MANAGER
	if (BuffTestFlag) {
		printm("req << pool[%s] byte[%lld]\n", BufferPoolStr[poolId], size);
	}
#endif // DEBUG_BUFFER_MANAGER

	*out = bufferListInfo;
	memset(bufferListInfo->buff, 0, bufferListInfo->size);

	unlockMutex(&gl_mutex);
	return 0;
}
void BuffRelease(BufferListInfo** buffList) {
	int err = 0;
	static uint64 cnt = 0;
	lockMutex(&gl_mutex);
#ifdef DEBUG_BUFFER_MANAGER
	if (gl_BitMap.Inited != TRUE) {
		printm("BufferManager 未初始化\n");
		exit(0);
	}
#endif // DEBUG_BUFFER_MANAGER
	if (buffList == NULL) {
		unlockMutex(&gl_mutex);
		return;
	}
	if (isInvalidBufferList((*buffList))) {
		ASSERT(TRUE, "invalid buffer!!!\n");
		exit(0);
	}
	if ((*buffList) == NULL) {
		unlockMutex(&gl_mutex);
		return;
	}
	if ((*buffList)->buff == NULL) {
		ASSERT(TRUE, "buffer handle not null, but dest ptr is null\n");
		unlockMutex(&gl_mutex);
		return;
	}
#ifdef DEBUG_BUFFER_MANAGER
	//if (BuffTestFlag) {
	//	printm("release >> pool[%s] byte[%lld]\n", BufferPoolStr[(*buffList)->poolId], (*buffList)->size);
	//}
#endif

	BufferPool* curPool = getPoolById((*buffList)->poolId);
#ifdef DEBUG_BUFFER_MANAGER
	if (curPool == NULL) {
		printm("ERROR: there is invalid poolId in bufferList\n");
		exit(0);
	}
#endif // DEBUG_BUFFER_MANAGER

	(*buffList)->userNum--;
	if ((*buffList)->userNum > 0) {
		ASSERT(TRUE, "user num > 0?");
		unlockMutex(&gl_mutex);
		return;
	}
	err = StaticListIndexRemove(curPool->listType, (*buffList) - gl_bufferList);
	if (err) {
		//never fail!
		ASSERT("BuffRelease error: StaticListIndexRemove fail[%d]\n", err);
		exit(0);
	}

	curPool->remainSize += (*buffList)->size;
	gl_remainSize += (*buffList)->size;
	gl_remainBufferListCnt++;
#ifdef DEBUG_BUFFER_MANAGER
	//char tmp[64] = { 0 };
	//formatBytes(curPool->remainSize, tmp);
	//printm("pool[%d] remain %s after release\n", (*buffList)->poolId, tmp);
#endif // DEBUG_BUFFER_MANAGER

#ifdef DEBUG_BUFFER_MANAGER
	//memset((*buffList)->buff, U8_MAX, (*buffList)->size);
#endif // DEBUG_BUFFER_MANAGER

	memset((*buffList), 0, sizeof(BufferListInfo));
	(*buffList) = NULL;

	unlockMutex(&gl_mutex);
	return;
}
void BuffMngSleep() {
	gl_BitMap.Sleeping = TRUE;
}
void BuffMngWakeup() {
	gl_BitMap.Sleeping = FALSE;
}

//扫描各buffer，根据使用情况调整buffer位置
int BuffScan() {
	if (!gl_BitMap.Sleeping || gl_size != gl_remainSize)
		return -1;
	resetPoolWeightGCD();

	return 0;
}
void DestroyBuffer() {
	if (gl_realBuffHead == NULL)
		return;
	free(gl_realBuffHead);
	gl_buff = NULL;
	gl_size = 0;
	gl_remainSize = 0;

	memset(&gl_BitMap, 0, sizeof(BufferManagerBitMap));
	if (gl_bufferList != NULL) {
		free(gl_bufferList);
	}
	gl_bufferListSize = 0;
	gl_remainBufferListCnt = 0;

	memset(gl_poolArr, 0,sizeof(gl_poolArr));
	gl_poolCnt = 0;
	gl_lastListType = STATIC_LIST_BUFFER_POOL0;
}
void BuffASyncIOIsGoingOn(BufferPool* pool) {
	pool->Bitmap.lock = TRUE;
	return;
}
void BuffASyncIODone(BufferPool* pool) {
	pool->Bitmap.lock = FALSE;
	return;
}
void DEBUG_PrintBuffer() {
	uint32 i;
	uint32 j;
	uint64 offset = 0;
	printm("-----Buffer debug-----\n");

	for (i = 0; i < BUFFER_POOL_MAX;i++) {
		char test[100] = { 0 };
		formatBytes(gl_poolArr[i].remainSize, test);
		printm("pool[%d] remain[%s]\n", i, test);
	}

	for (i = 0; i < gl_poolCnt; i++) {
		//遍历所有pool
		
		uint16 head = StaticListIndexGetHeadByListType(gl_poolArr[i].listType);
		if (head == U16_MAX) {
			//printm("null pool\n", i);
			continue;
		}
		printm("pool[%d]\n", i);
		uint16 tmp = head;
		uint16 next = STATIC_LIST_INVALID_VALUE;

		if (gl_bufferList[head].buff > gl_poolArr[i].buffHead) {
			printm("[****(%lldByte)]", gl_bufferList[head].buff - gl_poolArr[i].buffHead);
		}
		while (tmp != STATIC_LIST_INVALID_VALUE) {
			next = StaticListIndexGetNextNode(gl_poolArr[i].listType, tmp);
			offset = gl_bufferList[tmp].buff - gl_buff;
			printm("[%lld-%lld(%lldByte)]", gl_bufferList[tmp].buff - (i == 0 ? gl_buff : gl_poolArr[i - 1].buffHead + gl_poolArr[i - 1].size), gl_bufferList[tmp].buff + gl_bufferList[tmp].size - (i == 0 ? gl_buff : gl_poolArr[i - 1].buffHead + gl_poolArr[i - 1].size), gl_bufferList[tmp].size);
			if (next != U16_MAX) {
				if (gl_bufferList[next].buff - gl_bufferList[tmp].buff > gl_bufferList[tmp].size) {
					//连段buffer不连续
					printm("*****");
				}
			}
			tmp = next;
		}
		printm("\n");
	}


	printm("--------------------------\n");
}
void DEBUG_TestCase() {
	int err = 0;
	err = initBufferManagserBySize(1, BUFFER_POOL_NORMAL, BYTE_1G);

	BufferListInfo* b1 = NULL;
	BufferListInfo* b2 = NULL;
	BufferListInfo* b3 = NULL;
	BufferListInfo* b4 = NULL;
	BufferListInfo* b5 = NULL;
	BufferListInfo* b6 = NULL;
	
	err = BuffRequest(10, BUFFER_POOL_NORMAL, &b1);
	memset(b1->buff, 1, b1->size);
	DEBUG_PrintBuffer();
	err = BuffRealloc(&b1, 20, BUFFER_POOL_NORMAL);
	memset(b1->buff, 1, b1->size);
	DEBUG_PrintBuffer();
	err = BuffRequest(5, BUFFER_POOL_NORMAL, &b2);
	memset(b2->buff, 2, b2->size);
	DEBUG_PrintBuffer();
	err = BuffRequest(10, BUFFER_POOL_NORMAL, &b3);//bug
	memset(b3->buff, 3, b3->size);
	DEBUG_PrintBuffer();
	err = BuffRequest(10, BUFFER_POOL_NORMAL, &b4);//bug
	memset(b4->buff, 4, b4->size);
	DEBUG_PrintBuffer();

	BuffRelease(&b3);
	DEBUG_PrintBuffer();

	err = BuffRequest(10, BUFFER_POOL_NORMAL, &b5);
	memset(b5->buff, 5, b5->size);
	DEBUG_PrintBuffer();

	err = BuffRequest(10, BUFFER_POOL_NORMAL, &b6);	//defrage test
	memset(b6->buff, 6, b6->size);
	DEBUG_PrintBuffer();

	return;
}
uint64 BuffGetUsedSizeByPool(BufferPoolId poolId) {
	BufferPool* pool =  getPoolById(poolId);
	return pool->size - pool->remainSize;
}
uint64 BuffGetPoolSize(BufferPoolId poolId) {
	BufferPool* pool = getPoolById(poolId);
	return pool->size;
}
int BuffGC(BufferPoolId poolId) {
	return defrageBuffInPool(poolId);
}
uint32 BuffGetRemainBufferListCnt() {
	return gl_remainBufferListCnt;
}

void BufferManager_test1_byteAlignment() {
	int err = 0;
	err = initBufferManagserBySize(1, BUFFER_POOL_NORMAL, BYTE_1M); ASSERT(err, "init fail");
	BufferListInfo* b1 = getBuffer(3, BUFFER_POOL_NORMAL);
	BufferListInfo* b2 = getBuffer(6, BUFFER_POOL_NORMAL);

	int64 n = (uint64)(b2->buff) % 8;
	if (n) {
		printm("fail");
	}
	else {
		printm("pass");
	}
	
	BuffRelease(&b1);
	BuffRelease(&b2);
}