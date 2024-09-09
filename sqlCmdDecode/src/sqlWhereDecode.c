#include "sqlWhereDecode.h"
#include "myOperation.h"
#include <malloc.h>
#include <string.h>
#include <stdio.h>

#define ERROR(errCode) {err = errCode;goto error;}
#define STACK_NODE_OP_VALUE(node) (*((operatType*)(node->value)))
#define DOUBLE_VALUE(ptr) (*((double*)(ptr)))
#define BOOL_VALUE(ptr) (*((uint8_t*)(ptr)))
#define OP_LEVEL(op) (opLevel[op])
#define INVALID_COL_ID (UINT32_MAX)
#define RANGE_ITEM_LIST_BLOCK_SIZE (10U)

int expressionWhereCmd(char* table, char* cmd, uint32_t cmdLen, char** outStream, uint32_t* size) {
	if (cmd == NULL || outStream == NULL || size == NULL)
		return -1;
	if (*outStream != NULL)
		return -1;
	int err = 0;

	char* ptr = cmd;
	stackNode* head = NULL;
	uint32_t headCnt = 0;

	err = sqlStrExpressionToStack(table, cmd, cmdLen, &head); if (err < 0) return -2;
	//stackShow(head);
	//»¯¼ò
	headCnt = stackNodeCnt(head);

	err = multipleExpressionSimplify(&head, headCnt, NULL);
	if (err < 0) {
		ERROR(-2);
	}

	char* buff = NULL;
	uint32_t buffSize = 0;
	err = stack2Buffer(head, &buff, &buffSize, NULL);

	*outStream = buff;
	*size = buffSize;
	stackFreeOperatorStack(&head);
	
	return 0;

error:
	stackFreeOperatorStack(&head);
	return err;
}
