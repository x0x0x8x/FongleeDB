#include "sqlCommandHandleHost.h"
#include "operatorTypeDef.h"
#include "MyStrTool.h"
#include "myStack.h"
#include "BufferListManager.h"
#include "myOperation.h"

#define ERROR(errCode) {err = errCode;goto error;}
#ifdef DEBUG_SQL_DECODE_HOST
#define ASSERT(condition,...) ASSERT_GL(condition,__VA_ARGS__);
#else
#define ASSERT(condition,...);
#endif // DEBUG_SQL_DECODE_HOST

#define BIT_AND(value, bit) (value & (0xffffffffu >> (32-bit)))
#define DOUBLE_VALUE(ptr) (*((double*)(ptr)))
#define FLOAT_VALUE(ptr) (*((float*)(ptr)))
#define INT8_VALUE(ptr) (*((int8_t*)(ptr)))
#define INT16_VALUE(ptr) (*((int16_t*)(ptr)))
#define INT32_VALUE(ptr) (*((int32_t*)(ptr)))
#define INT64_VALUE(ptr) (*((int64_t*)(ptr)))
#define OP_LEVEL(op) (getOpLevel(op))
#define INVALID_COL_ID (UINT32_MAX)
#define RANGE_ITEM_LIST_BLOCK_SIZE (10U)

static void viewUpdateTokens(SqlTokenHandle* cmd);
static void viewExpStack(BufferListInfo* head);
static int getColumnDataByName(uint64 tableId, char* row, char* columnName, uint32 len, BufferListInfo** outValue, SqlHandleGetColumnValueByNameCB callback);

static BufferListInfo* hostGetNextEqualNode(BufferListInfo* stack, uint32 cnt, uint32* index);
static BufferListInfo* hostGetLastAndOr(BufferListInfo* stack, uint32 cnt, BufferListInfo* node);
static BufferListInfo* hostGetNextAndOr(BufferListInfo* stack, uint32 cnt);
static BufferListInfo* hostGetNextContinuousAndExpression(BufferListInfo* stack, uint32 cnt, uint32* outCnt);
static BufferListInfo* hostGetNextBracketMultipleExpressionInStack(BufferListInfo* head, uint32 cnt, uint32* outCnt);

static BufferListInfo* expStackGetNextOperator(BufferListInfo* stack, uint32 cnt, operatType opType);
static BufferListInfo* expStackGetNextBracket(BufferListInfo* stack, uint32 cnt, uint32* outCnt);
static BufferListInfo* expStackGetLastOperator(BufferListInfo* stack, uint32 cnt, stackNode* node, operatType opType);
static BufferListInfo* expStackGetLastAndOr(BufferListInfo* stack, uint32_t cnt, BufferListInfo* node);
static BufferListInfo* expStackGetNextUnkownNode(BufferListInfo* head, uint32 cnt);
static int hostCalculateExpStack(BufferListInfo** head, bool* result);
static int scanExpStackAndExchangeVarNode(uint64 tableId, char* row, BufferListInfo** head, SqlHandleGetColumnValueByNameCB callback) {
	int err = 0;
	BufferListInfo* last = NULL;
	BufferListInfo* tmp = *head;
	bool hasBoolItem = FALSE;
	//viewExpStack(*head); printm("\n");

#define GET_SUB_EXP(info) (*((BufferListInfo**)(info + 1)))

	while (tmp) {
		valueInfo* info = tmp->buff;
		if (info->valueType == VT_Unkown) {
			BufferListInfo* subExp = GET_SUB_EXP(info);
			err = scanExpStackAndExchangeVarNode(tableId, row, &subExp, callback); 
			if (err < 0) { return -2; }
			uint32 subCnt = BuffListMgrGetListSize(subExp);
			
			if (err) {
				//bool exp
				err = hostCalculateMultipleBoolExpression(&subExp, subCnt, &subCnt); if (err <= 0) { return -2; }
				if (last) {
					//viewExpStack(*head); printm("\n");
					BufferListInfo* t2 = last->next;
					last->next = NULL;
					err = expStackPushBool(&last, err == 1 ? TRUE : FALSE); if (err < 0) { return -2; }
					last->next->next = t2->next;
					t2->next = NULL;
					BuffRelease(&t2);
					tmp = last->next;
					//viewExpStack(*head); printm("\n");

				}
				else {
					//viewExpStack(*head); printm("\n");
					BufferListInfo* t2 = (*head)->next;
					(*head)->next = NULL;
					BuffRelease(head);
					err = expStackPushBool(head, err == 1 ? TRUE : FALSE); if (err < 0) { return -2; }
					(*head)->next = t2;
					tmp = (*head);
					//viewExpStack(*head); printm("\n");
				}
				
				hasBoolItem = TRUE;
				//viewExpStack(*head); printm("\n");
			}
			else {
				//normal exp
				err = hostSingleExpressionSimplify(&subExp, subCnt, &subCnt); if (err < 0) { return -2; }
				ASSERT(err == 0, "无法化简的表达式");
				ASSERT(err != 1 && err != 2, "invalid expression");

				if (last) {
					//viewExpStack(*head); printm("\n");
					BufferListInfo* t2 = last->next;
					last->next = NULL;
					err = expStackPushBool(&last, err == 1 ? TRUE : FALSE);
					last->next->next = t2->next;
					t2->next = NULL;
					BuffRelease(&t2);
					tmp = last->next;
					//viewExpStack(*head); printm("\n");
				}
				else {
					//viewExpStack(*head); printm("\n");
					BufferListInfo* t2 = (*head)->next;
					(*head)->next = NULL;
					BuffRelease(head);
					err = expStackPushBool(head, err == 1 ? TRUE : FALSE);
					(*head)->next = t2;
					tmp = (*head);
					//viewExpStack(*head); printm("\n");
				}
				//viewExpStack(*head); printm("\n");
			}
			hasBoolItem = TRUE;
			BuffRelease(&subExp);
			memset(info + 1, 0, sizeof(BufferListInfo*));
			if (last) {
				valueInfo* lastInfo = last->buff;
				if (lastInfo->valueType == VT_operator && lastInfo->opType == OT_Left_bracket) {
					//remove ( )
					valueInfo* ringBInfo = last->next->next->buff;
					ASSERT(ringBInfo->valueType != VT_operator || ringBInfo->opType != OT_Right_bracket, "invalie ()");
					BufferListInfo* llast = BuffListMgrGetLastNode(*head, last);
					BufferListInfo* t2 = NULL;
					if (llast) {
						t2 = llast->next;//t2 -> (
						llast->next = llast->next->next;
						BuffRelease(&t2);
						t2 = llast->next->next;//t2 -> )
						llast->next->next = t2->next;
						BuffRelease(&t2);
						tmp = llast->next;
						last = llast;
					}
					else {
						t2 = (*head)->next;//head -> (
						BuffRelease(head);
						(*head) = t2;
						t2 = (*head)->next;//t2 -> )
						(*head)->next = t2->next;
						BuffRelease(&t2);
						tmp = (*head);
						last = NULL;
					}
				}
			}
		}
		else {
			if (info->valueType == VT_var) {
				//viewExpStack(*head); printm("\n");

				//exchange var to real value
				BufferListInfo* real = NULL;
				char* varName = info + 1;
				uint64 varSize = tmp->size - (varName - tmp->buff);
				//printm("%.*s\n",varSize, varName);
				err = getColumnDataByName(tableId, row, varName, varSize, &real, callback); if (err) { return -2; }
				//real has inited datatype and valuetype
				
				real->next = tmp->next;
				if (last) {
					last->next = real;
				}
				if (tmp == *head) {
					BuffRelease(&tmp);
					*head = real;
				}
				else {
					BuffRelease(&tmp);
				}
				
				tmp = real;
				//viewExpStack(*head); printm("\n");
			}

		}
		last = tmp;
		tmp = tmp->next;
	}
	//viewExpStack(*head); printm("\n");
	return hasBoolItem;
}
int hostCalculateNumberExpression(BufferListInfo** head);
int hostCalculateMultipleExpression(BufferListInfo** head, uint32 cnt, uint32* outCnt);
int hostCalculateContinuousAndExpression(BufferListInfo** head, uint32 cnt, uint32* outCnt);
int hostCalculateContinuousOrExpression(BufferListInfo** head, uint32 cnt, uint32* outCnt);
int hostCalculateMultipleBoolExpression(BufferListInfo** head, uint32 cnt, uint32* outCnt);
int hostSingleExpressionToBool(BufferListInfo** head);
int hostSingleExpressionSimplify(BufferListInfo** head, uint32 cnt, uint32* newCnt);

static int whereStreamSubExpHandle(char* ptr, BufferListInfo** head, char** next) {
	int err = 0;
	stackNode* subExp = ptr;
	stackNode* node = subExp + 1;
	char* value = node + 1;
	BufferListInfo* tmp = NULL;
	for (uint32 i = 0; i < subExp->size;i++) {
		if (node->type.valueType == VT_Unkown) {
			BufferListInfo* subUnkownExp = NULL;
			char* nextPtr = NULL;
			err = whereStreamSubExpHandle(node, &subUnkownExp, &nextPtr);
			if (node == subExp + 1) {
				(*head) = getBuffer(sizeof(valueInfo) + sizeof(BufferListInfo*), BUFFER_POOL); if (!(*head)) { return -2; }
				tmp = (*head);
			}
			else {
				tmp->next = getBuffer(sizeof(valueInfo) + sizeof(BufferListInfo*), BUFFER_POOL); if (!tmp) { return -2; }
				tmp = tmp->next;
			}
			
			valueInfo* subInfo = tmp->buff;
			memcpy(subInfo, &node->type, sizeof(*subInfo));
			memcpy(subInfo + 1, subUnkownExp, sizeof(BufferListInfo*));

			node = nextPtr;
			value = node + 1;
		}
		else {
			if (node == subExp + 1) {
				(*head) = getBuffer(sizeof(valueInfo) + node->size, BUFFER_POOL); if (!(*head)) { return -2; }
				tmp = (*head);
			}
			else {
				tmp->next = getBuffer(sizeof(valueInfo) + node->size, BUFFER_POOL); if (!tmp) { return -2; }
				tmp = tmp->next;
			}
			
			valueInfo* info = tmp->buff;
			memcpy(info, &node->type, sizeof(*info));
			memcpy(info + 1, value, node->size);

			node = value + node->size;
			value = node + 1;
		}
	}

	*next = node;
	return 0;
}
static int whereStreamToStack(char* ptr, uint64 size, BufferListInfo** head) {
	int err = 0;
	stackNode* node = ptr;
	char* data = node + 1;
	BufferListInfo* tmp = *head;
	while ((char*)node - ptr < size) {
		if (node->type.valueType != VT_Unkown) {
			if (node == ptr) {
				(*head) = getBuffer(sizeof(valueInfo) + node->size, BUFFER_POOL);
				tmp = *head; 
			}
			else {
				tmp->next = getBuffer(sizeof(valueInfo) + node->size, BUFFER_POOL);
				tmp = tmp->next;
			}
			if (!tmp) { return -2; }
			valueInfo* info = tmp->buff;
			char* value = info + 1;
			memcpy(info, &node->type, sizeof(node->type));
			memcpy(value, data, node->size);

			node = data + node->size;
			data = node + 1;
		}
		else {
			BufferListInfo* subExp = NULL;
			char* next = NULL;
			
			err = whereStreamSubExpHandle(node, &subExp, &next);
			if (node == ptr) {
				(*head) = getBuffer(sizeof(valueInfo) + sizeof(BufferListInfo*), BUFFER_POOL);
				tmp = *head;
			}
			else {
				tmp->next = getBuffer(sizeof(valueInfo) + sizeof(BufferListInfo*), BUFFER_POOL);
				tmp = tmp->next;
			}
			if (!tmp) { return -2; }

			valueInfo* info = tmp->buff;
			memcpy(info, &node->type, sizeof(*info));
			memcpy(info + 1, &subExp, sizeof(BufferListInfo*));

			node = next;
			data = node + 1;
		}		
	}

	return 0;
}
static uint32 getSetExpCnt(char* ptr) {
	SqlCmdTag* tag = ptr;
	uint32 ret = 0;
	while (1) {
		if (tag->cmd != SQL_CMD_SET) { break; }
		char* exp = tag + 1;
		tag = exp + tag->size;
		ret++;
	}
	return ret;
}
static int setExpStreamToStack(char* ptr, uint64 size, BufferListInfo** head) {
	return whereStreamToStack(ptr, size, head);
}
static int decodeSelectStream(char* stream, uint64 size, SqlTokenHandle* tk) {
	// select col1,col2 from table1 where 3*2=6 & 1+2=3 & col3=8 & ((6*3)+col3*(4-1)>8-3+(6*2)/2) | (2*6<8 | 30>0) & (8-2<0)
	
	int err = 0;
	SqlCmdTag* tag = stream;
	
	char* data = tag + 1;
	char* colStrList = NULL;
	uint32 colStrListLen = 0;
	char* tableStr = NULL;
	uint32 tableStrLen = 0;
	char* wherePtr = NULL;
	uint32 whereSize = 0;
	
	ASSERT(tag->cmd != SQL_CMD_SELECT, "not select cmd");
	colStrList = tag + 1;
	colStrListLen = tag->size;

	tag = colStrList + tag->size;
	ASSERT(tag->cmd != SQL_CMD_FROM, "not from cmd");
	tableStr = tag + 1;
	tableStrLen = tag->size;

	tag = tableStr + tag->size;
	if ((char*)tag - stream < size) {
		ASSERT(tag->cmd != SQL_CMD_WHERE, "not where cmd");
		wherePtr = tag + 1;
		whereSize = tag->size;
	}

	tk->tableName = getBuffer(tableStrLen, BUFFER_POOL); if (!tk->tableName) { return -2; }
	tk->destColList = getBuffer(colStrListLen, BUFFER_POOL); if (!tk->destColList) { return -2; }
	memcpy(tk->tableName->buff, tableStr, tableStrLen);
	memcpy(tk->destColList->buff, colStrList, colStrListLen);
	err = whereStreamToStack(wherePtr, whereSize, &tk->whereStack); if (err) { return -3; }

	return 0;
}
static int decodeUpdateStream(char* stream, uint64 size, SqlTokenHandle* tk) {
	//tag(update) + tableName + tag(set) + stack(col1 = col2 + col3)... + tag(where) + whereExpressionStack
	int err = 0;
	SqlCmdTag* tag = stream;
	if (tag->cmd != SQL_CMD_UPDATE) {
		return -1;
	}
	tk->type = SQL_CMD_UPDATE;
	char* tableName = tag + 1;
	uint32 tableNameLen = tag->size;
	
	tag = tableName + tag->size;
	
	uint32 setCnt = getSetExpCnt(tag);
	tk->setList = getBuffer(setCnt * sizeof(BufferListInfo*), BUFFER_POOL); if (!tk->setList) { return -2; }
	BufferListInfo** setStack = tk->setList->buff;
	while (tag->cmd == SQL_CMD_SET) {
		char* setStackStream = tag + 1;
		BufferListInfo* tmp = NULL;
		err = setExpStreamToStack(setStackStream, tag->size, &tmp); if (err) { freeBuffer(&tmp); return -2; }
		memcpy(setStack, &tmp, sizeof(BufferListInfo*));
		tag = setStackStream + tag->size;
		viewExpStack(tmp); printm("\n");
		setStack++;
	}
	
	if ((char*)tag - stream < size) {
		if (tag->cmd != SQL_CMD_WHERE) {
			return -1;
		}
		BufferListInfo* expStack = NULL;
		err = whereStreamToStack(tag + 1, tag->size, &expStack); if (err) { return -1; }
		tk->whereStack = expStack;
		//viewExpStack(expStack); printm("\n");
	}

	tk->tableName = getBuffer(tableNameLen, BUFFER_POOL); if (!tk->tableName) { return -2; }
	memcpy(tk->tableName->buff, tableName, tableNameLen);

	return 0;
}
static int decodeCreateStream(char* stream, uint64 size, SqlTokenHandle* tk) {
	//create table table1 SqlColumnItem[]={col1 int8 index, col2 string(20), col3 float }
	SqlCmdTag* tag = stream;
	if (tag->cmd != SQL_CMD_CREATE) {
		return -1;
	}
	tk->type = SQL_CMD_CREATE;
	char* tableName = tag + 1;
	uint32 tableNameLen = tag->size;
	tag = tableName + tag->size;

	if (tag->cmd != SQL_CMD_COL_INFO) {
		return -1;
	}
	uint32 cnt = tag->size / sizeof(SqlColumnItem);
	SqlColumnItem* item = tag + 1;
	tk->columnInfoList = getBuffer(tag->size, BUFFER_POOL); if (!tk->columnInfoList) { return -2; }
	memcpy(tk->columnInfoList->buff, item, tk->columnInfoList->size);
	
	tk->tableName = getBuffer(tableNameLen, BUFFER_POOL); if (!tk->tableName) { return -2; }
	memcpy(tk->tableName->buff, tableName, tableNameLen);

	return 0;
}
static int decodeInsertStream(char* stream, uint64 size, SqlTokenHandle* tk) {
	SqlCmdTag* tag = stream;
	if (tag->cmd != SQL_CMD_INSERT) {
		return -1;
	}
	tk->type = SQL_CMD_INSERT;
	char* tableName = tag + 1;
	uint32 tableNameLen = tag->size;

	tag = tableName + tag->size;
	if (tag->cmd != SQL_CMD_VALUE) {
		return -1;
	}
	char* colList = tag + 1;
	tk->columnInfoList = getBuffer(tag->size, BUFFER_POOL); if (!tk->columnInfoList) { return -2; }
	memcpy(tk->columnInfoList->buff, colList, tag->size);

	tk->tableName = getBuffer(tableNameLen, BUFFER_POOL); if (!tk->tableName) { return -2; }
	memcpy(tk->tableName->buff, tableName, tableNameLen);

	return 0;
}
static int decodeDeleteStream(char* stream, uint64 size, SqlTokenHandle* tk) {
	int err = 0;
	SqlCmdTag* tag = stream;
	if (tag->cmd != SQL_CMD_DELETE) {
		return -1;
	}
	tk->type = SQL_CMD_DELETE;
	char* tableName = tag + 1;
	uint32 tableNameLen = tag->size;
	tk->tableName = getBuffer(tableNameLen, BUFFER_POOL); if (!tk->tableName) { return -2; }
	memcpy(tk->tableName->buff, tableName, tableNameLen);

	tag = tableName + tag->size;
	if (tag->cmd != SQL_CMD_WHERE) {
		return -1;
	}
	char* whereStr = tag + 1;
	uint32 whereStrSize = tag->size;
	BufferListInfo* expStack = NULL;
	err = whereStreamToStack(whereStr, whereStrSize, &expStack); if (err) { return -3; }
	tk->whereStack = expStack;

	return 0;
}
static int decodeDropStream(char* stream, uint64 size, SqlTokenHandle* tk) {
	int err = 0;
	SqlCmdTag* tag = stream;
	if (tag->cmd != SQL_CMD_DROP) {
		return -1;
	}
	tk->type = SQL_CMD_DROP;
	char* tableName = tag + 1;
	uint32 tableNameLen = tag->size;
	tk->tableName = getBuffer(tableNameLen, BUFFER_POOL); if (!tk->tableName) { return -2; }
	memcpy(tk->tableName->buff, tableName, tableNameLen);

	return 0;
}
static int decodeShowTables(char* stream, uint64 size, SqlTokenHandle* tk) {
	int err = 0;
	SqlCmdTag* tag = stream;
	if (tag->cmd != SQL_CMD_SHOW_TABLES) {
		return -1;
	}
	tk->type = SQL_CMD_SHOW_TABLES;
	if (tag->size) {
		tk->destTableList = getBuffer(tag->size, BUFFER_POOL); if (!tk->destTableList) { return -2; }
		memcpy(tk->destTableList->buff, tag + 1, tag->size);
	}
	return 0;
}
static int decodeShowColumns(char* stream, uint64 size, SqlTokenHandle* tk) {
	int err = 0;
	SqlCmdTag* tag = stream;
	if (tag->cmd != SQL_CMD_SHOW_COLUMNS) {
		return -1;
	}
	tk->type = SQL_CMD_SHOW_COLUMNS;
	char* colListStr = tag + 1;
	uint32 colListStrLen = tag->size;

	tag = colListStr + tag->size;
	char* tableName = tag + 1;
	uint32 tableNameLen = tag->size;

	tk->tableName = getBuffer(tableNameLen, BUFFER_POOL); if (!tk->tableName) { ERROR(-3); }
	memcpy(tk->tableName->buff, tableName, tableNameLen);
	if (colListStrLen == 1 && colListStr[0] == '*' || colListStrLen == 0) {
		//all
	}
	else {
		tk->destColList = getBuffer(colListStrLen, BUFFER_POOL); if (!tk->destColList) { ERROR(-3); }
		memcpy(tk->destColList->buff, colListStr, colListStrLen);
	}
	
	return 0;

error:
	BuffRelease(&tk->destColList);
	BuffRelease(&tk->tableName);

	return -3;
}
static int decodeStream(char* stream, uint64 size, SqlTokenHandle* tk) {
	// select col1,col2 from table1 where 3*2=6 & 1+2=3 & col3=8 & ((6*3)+col3*(4-1)>8-3+(6*2)/2) | (2*6<8 | 30>0) & (8-2<0)
	// create table table1 ( col1 int8 index, col2 string(20), col3 float )
	// insert into table1 (col1, col2, ...) value ('str1', 123, ...)
	// update table1 set col1 = col2+col3, col2 = 25 where condition
	// delete from table1 where condition
	// drop table table1
	SqlCmdTag* tag = stream;
	switch (tag->cmd) {
	case SQL_CMD_SELECT:
		return decodeSelectStream(stream, size, tk);
	case SQL_CMD_UPDATE:
		return decodeUpdateStream(stream, size, tk);
	case SQL_CMD_CREATE:
		return decodeCreateStream(stream, size, tk);
	case SQL_CMD_INSERT:
		return decodeInsertStream(stream, size, tk);
	case SQL_CMD_DELETE:
		return decodeDeleteStream(stream, size, tk);
	case SQL_CMD_DROP:
		return decodeDropStream(stream, size, tk);
	case SQL_CMD_SHOW_TABLES:
		return decodeShowTables(stream, size, tk);
	case SQL_CMD_SHOW_COLUMNS:
		return decodeShowColumns(stream, size, tk);
	default:
		return -1;
	}

	

	return 0;
}

static void viewDataByType(sqlDataType type, char* value, uint32 size) {
	switch (type)
	{
	case SQL_DATA_STRING: {
		printm("'%.*s'", size, value);
		break;
	}
	case SQL_DATA_INT8:
		printm("%d", *((int8*)value));
		break;
	case SQL_DATA_INT16:
		printm("%d", *((int16*)value));
		break;
	case SQL_DATA_INT32:
		printm("%d", *((int32*)value));
		break;
	case SQL_DATA_INT64:
		printm("%lld", *((int64*)value));
		break;
	case SQL_DATA_FLOAT:
		printm("%g", *((float*)value));
		break;
	case SQL_DATA_DOUBLE:
		printm("%g", *((double*)value));
		break;
	case SQL_DATA_BINARY:
	case SQL_DATA_IMAGE:
		printm("%.*s", size, value);
		break;
	default:
		printm("invalid");
		break;
	}
}
static void showStackNodeHost(valueInfo* info, char* value, uint32 size) {

	switch (info->valueType)
	{
	case VT_operator: {
		uint8 ot = info->opType & (0xfu);
		switch (ot)
		{
		case OT_Plus:
			printm("+");
			break;
		case OT_Minus:
			printm("-");
			break;
		case OT_Multiplication:
			printm("*");
			break;
		case OT_Division:
			printm("/");
			break;
		case OT_Greater_than:
			printm(">");
			break;
		case OT_Less_than:
			printm("<");
			break;
		case OT_Greater_than_or_equal:
			printm(">=");
			break;
		case OT_Less_than_or_equal:
			printm("<=");
			break;
		case OT_Equal:
			printm("=");
			break;
		case OT_Or:
			printm("|");
			break;
		case OT_And:
			printm("&");
			break;
		case OT_Left_bracket:
			printm("(");
			break;
		case OT_Right_bracket:
			printm(")");
			break;
		default:
			printm("[invalid]");
			return;
		}
	}
					break;
	case VT_var:
		printm("%.*s", size, value);
		break;
	case VT_num: {
		uint32 type = info->dataType & 0xfu;
		switch (type)
		{
		case SQL_DATA_INT8:
			printm("%d", *((int8*)(value)));
			break;
		case SQL_DATA_INT16:
			printm("%d", *((int16*)(value)));
			break;
		case SQL_DATA_INT32:
			printm("%d", *((int32*)(value)));
			break;
		case SQL_DATA_INT64:
			printm("%lld", *((int64*)(value)));
			break;
		case SQL_DATA_FLOAT:
			printm("%g", *((float*)(value)));
			break;
		case SQL_DATA_DOUBLE:
			printm("%g", *((double*)(value)));
			break;
		case SQL_DATA_TABLE:
			printm("%u", *((uint32*)(value)));
			break;
		default:
			printm("[invalid]");
			return;
		}
		break;
	}
	case VT_binary:
		printm("\'%.*s\'", size, value);
		break;
	case VT_Bool:
		if (info->isTrue & 0x1u) {
			printm("true");
		}
		else {
			printm("false");
		}
		break;
	case VT_Unkown:
		printm("[unkown]");
		break;
	default:
		printm("[invalid]");
		break;
	}
}
static void printSqlDataType(sqlDataType type) {
	switch (type)
	{
	case SQL_DATA_STRING:
		printm("string");
		break;
	case SQL_DATA_INT8:
		printm("int8");
		break;
	case SQL_DATA_INT16:
		printm("int16");
		break;
	case SQL_DATA_INT32:
		printm("int32");
		break;
	case SQL_DATA_INT64:
		printm("int64");
		break;
	case SQL_DATA_FLOAT:
		printm("float");
		break;
	case SQL_DATA_DOUBLE:
		printm("double");
		break;
	case SQL_DATA_BINARY:
		printm("binary");
		break;
	default:
		printm("invalid");
		break;
	}
}
static void viewExpStack(BufferListInfo* head) {
	BufferListInfo* tmp = head;
#define GET_SUB_EXP(info) (*((BufferListInfo**)(info + 1)))
	while (tmp) {
		valueInfo* info = tmp->buff;
		char* value = info + 1;
		uint32 size = tmp->size - sizeof(*info);
		if (info->valueType == VT_Unkown) {
			BufferListInfo* subExp = GET_SUB_EXP(info);
			printm("[sub]");
			viewExpStack(subExp);
		}
		else {
			showStackNodeHost(info, value, size);
		}
		tmp = tmp->next;
	}

}
static void viewSelectTokens(SqlTokenHandle* tk) {
	printm("*** select table[%.*s] ***\n", tk->tableName->size, tk->tableName->buff);
	printm("dest cols: %.*s\n", tk->destColList->size, tk->destColList->buff);
	if(tk->whereStack)
		printm("where: "); viewExpStack(tk->whereStack); printm("\n");
}
static void viewUpdateTokens(SqlTokenHandle* tk) {
	if (tk->type != SQL_CMD_UPDATE) {
		printm("not update sql\n");
		return;
	}
	printm("*** update table[%.*s] ***\n", tk->tableName->size, tk->tableName->buff);
	uint32 cnt = tk->setList->size / sizeof(BufferListInfo*);
#define GET_SET_EXP(buff) (*((BufferListInfo**)(buff)))
	BufferListInfo** setItem = tk->setList->buff;
	for (uint32 i = 0; i < cnt;i++, setItem++) {
		BufferListInfo* tmp = GET_SET_EXP(setItem);
		printm("set ");	viewExpStack(tmp); printm("\n");
	}
	printm("where "); viewExpStack(tk->whereStack); printm("\n");

}
static void viewCreateTokens(SqlTokenHandle* tk) {
	if (tk->type != SQL_CMD_CREATE) {
		printm("not create sql\n");
		return;
	}
	printm("*** create table[%.*s] ***\n", tk->tableName->size, tk->tableName->buff);
	uint32 cnt = tk->columnInfoList->size / sizeof(SqlColumnItem);
	SqlColumnItem* col = tk->columnInfoList->buff;
	for (uint32 i = 0; i < cnt; i++, col++) {
		printm("[");
		sqlCmdViewColumnInfo(col);
		printm("]");
	}
	printm("\n");

}
static void viewInsertTokens(SqlTokenHandle* tk) {
	if (tk->type != SQL_CMD_INSERT) {
		printm("not insert sql\n");
		return;
	}
	printm("*** insert table[%.*s] ***\n", tk->tableName->size, tk->tableName->buff);
	SqlInsertColumnInfo* col = tk->columnInfoList->buff;
	while ((char*)col - tk->columnInfoList->buff < tk->columnInfoList->size) {
		char* value = col + 1;
		printm("[%s ", col->name); viewDataByType(col->type, value, col->dataLen); printm("]");
		col = value + col->dataLen;
	}

	printm("\n");
}
static void viewDeleteTokens(SqlTokenHandle* tk) {
	if (tk->type != SQL_CMD_DELETE) {
		printm("not delete sql\n");
		return;
	}
	printm("*** delete from table[%.*s] ***\n", tk->tableName->size, tk->tableName->buff);
	printm("where: "); viewExpStack(tk->whereStack); printm("\n");
}
static void viewDropTokens(SqlTokenHandle* tk) {
	if (tk->type != SQL_CMD_DROP) {
		printm("not drop sql\n");
		return;
	}
	printm("*** drop table[%.*s] ***\n", tk->tableName->size, tk->tableName->buff);
}
static void viewShowTablesTockens(SqlTokenHandle* tk) {
	if (tk->type != SQL_CMD_SHOW_TABLES) {
		printm("not show tables sql\n");
		return;
	}
	printm("*** show tables[%.*s] ***\n", tk->destTableList?tk->destTableList->size:1, tk->destTableList?tk->destTableList->buff:"*");
}
static void viewShowColumnsTockens(SqlTokenHandle* tk) {
	if (tk->type != SQL_CMD_SHOW_COLUMNS) {
		printm("not show columns sql\n");
		return;
	}
	printm("*** show columns %.*s from %.*s***\n", tk->destColList ? tk->destColList->size : 1, tk->destColList ? tk->destColList->buff : "*", tk->tableName->size, tk->tableName->buff);
}
static void viewSqlTokenHandle(SqlTokenHandle* tk) {
	switch (tk->type) {
	case SQL_CMD_SELECT:
		viewSelectTokens(tk);
		break;
	case SQL_CMD_UPDATE:
		viewUpdateTokens(tk);
		break;
	case SQL_CMD_CREATE:
		viewCreateTokens(tk);
		break;
	case SQL_CMD_INSERT:
		viewInsertTokens(tk);
		break;
	case SQL_CMD_DELETE:
		viewDeleteTokens(tk);
		break;
	case SQL_CMD_DROP:
		viewDropTokens(tk);
		break;
	case SQL_CMD_SHOW_TABLES:
		viewShowTablesTockens(tk);
		break;
	case SQL_CMD_SHOW_COLUMNS:
		viewShowColumnsTockens(tk);
		break;
	default:
		printm("unkown sql type\n");
	}
}

static void freeExpStack(BufferListInfo** head) {
	BufferListInfo* tmp = *head;
#define GET_SUB_EXP(info) (*((BufferListInfo**)(info + 1)))
	while (tmp) {
		//viewExpStack(tmp); printm("\n");
		valueInfo* info = tmp->buff;
		if (info->valueType == VT_Unkown) {
			BufferListInfo* sub = GET_SUB_EXP(info);
			freeExpStack(&sub);
			memset(info + 1, 0, sizeof(BufferListInfo*));
		}

		BufferListInfo* next = tmp->next;
		if (tmp == *head) {
			BuffRelease(head);
		}
		else {
			BuffRelease(&tmp);
		}

		tmp = next;
	}
	
}
static void freeSelectTokens(SqlTokenHandle* tk) {
	BuffRelease(&tk->tableName);
	BuffRelease(&tk->destColList);
	freeExpStack(&tk->whereStack);
	memset(tk, 0, sizeof(*tk));
}
static void freeUpdateSetStack(BufferListInfo** head) {
	return freeExpStack(head);
}
static void freeUpdateTokens(SqlTokenHandle* tk) {
	BuffRelease(&tk->tableName);
	uint32 cnt = tk->destColList->size / sizeof(BufferListInfo*);
#define GET_SET_EXP(buff) (*((BufferListInfo**)(buff)))
	BufferListInfo** setItem = tk->destColList->buff;
	for (uint32 i = 0; i < cnt; i++, setItem++) {
		BufferListInfo* tmp = GET_SET_EXP(setItem);
		freeUpdateSetStack(&(tmp));
	}
	BuffRelease(&tk->setList);
	if (tk->whereStack) {
		freeExpStack(&tk->whereStack);
	}
	memset(tk, 0, sizeof(*tk));
}
static void freeCreateTokens(SqlTokenHandle* tk) {
	BuffRelease(&tk->tableName);
	BuffRelease(&tk->columnInfoList);
	memset(tk, 0, sizeof(*tk));
}
static void freeInsdertTokens(SqlTokenHandle* tk) {
	return freeCreateTokens(tk);
}
static void freeDeleteTokens(SqlTokenHandle* tk) {
	BuffRelease(&tk->tableName);
	freeExpStack(&tk->whereStack);
	BuffRelease(&tk->whereStack);
	memset(tk, 0, sizeof(*tk));
}
static void freeDropTokens(SqlTokenHandle* tk) {
	BuffRelease(&tk->tableName);
	memset(tk, 0, sizeof(*tk));
}
static void freeShowTables(SqlTokenHandle* tk) {
	BuffRelease(&tk->destTableList);
	memset(tk, 0, sizeof(*tk));
}
static void freeShowColumns(SqlTokenHandle* tk) {
	BuffRelease(&tk->destColList);
	BuffRelease(&tk->tableName);
	memset(tk, 0, sizeof(*tk));
}
static void freeSqlTokenHandle(SqlTokenHandle* tk) {
	switch (tk->type) {
	case SQL_CMD_SELECT:
		freeSelectTokens(tk);
		break;
	case SQL_CMD_UPDATE:
		freeUpdateTokens(tk);
		break;
	case SQL_CMD_CREATE:
		freeCreateTokens(tk);
		break;
	case SQL_CMD_INSERT:
		freeCreateTokens(tk);
		break;
	case SQL_CMD_DELETE:
		freeDeleteTokens(tk);
		break;
	case SQL_CMD_DROP:
		freeDropTokens(tk);
		break;
	case SQL_CMD_SHOW_TABLES:
		freeShowTables(tk);
		break;
	case SQL_CMD_SHOW_COLUMNS:
		freeShowColumns(tk);
		break;
	default:
		printm("unkown sql type\n");
	}
}

static int getColumnDataByName(uint64 tableId, char* row, char* columnName, uint32 len, BufferListInfo** outValue, SqlHandleGetColumnValueByNameCB callback) {
	return callback(tableId, row, columnName, len, outValue);
}
BufferListInfo* expStackPopTail(BufferListInfo** head) {
	if (*head == NULL) { return NULL; }
	uint32 cnt = BuffListMgrGetListSize(*head);
	BufferListInfo* tail = BuffListMgrGetTailNode(*head, cnt);
	if (cnt == 1) {
		*head = NULL;
	}
	else {
		BufferListInfo* tailLast = BuffListMgrGetLastNode(*head, tail);
		tailLast->next = NULL;
	}
	
	return tail;
}
int expStackPush(BufferListInfo** stack, valueInfo info, char* value, uint32 size) {
	if (*stack == NULL) {
		(*stack) = getBuffer(sizeof(valueInfo) + size, BUFFER_POOL);
		char* ptr = (*stack)->buff;
		memcpy(ptr, &info, sizeof(info));	ptr += sizeof(info);
		if(value)
			memcpy(ptr, value, size);
		return 0;
	}
	uint32 cnt = BuffListMgrGetListSize(*stack);
	BufferListInfo* tail = BuffListMgrGetTailNode(*stack, cnt);
	tail->next = getBuffer(sizeof(valueInfo) + size, BUFFER_POOL);
	tail = tail->next;

	char* ptr = tail->buff;
	memcpy(ptr, &info, sizeof(info));	ptr += sizeof(info);
	if (value)
		memcpy(ptr, value, size);
	return 0;
}
int expStackPushOperatior(BufferListInfo** stack, operatType operationType) {
	if (stack == NULL || operationType <= OT_Invalid || operationType >= OT_MAX)
		return -1;
	valueInfo info = { 0 };
	info.valueType = VT_operator;
	info.opType = operationType;
	int err = expStackPush(stack, info, NULL, 0);
	if (err < 0)
		return -2;

	return 0;
}
int expStackPushNumber(BufferListInfo** stack, sqlDataType dataType, void* value) {
	if (stack == NULL || dataType < SQL_DATA_INT8 || dataType > SQL_DATA_DOUBLE || value == NULL)
		return -1;
	int err = 0;
	valueInfo type = { 0 };
	type.valueType = VT_num;
	type.dataType = dataType;
	double* doublePtr = value;
	int64* intPtr = value;
	switch (dataType)
	{
	case SQL_DATA_INT8: {
		int8 realValue = *intPtr;
		err = expStackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_INT16: {
		int16 realValue = *intPtr;
		err = expStackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_INT32: {
		int32 realValue = *intPtr;
		err = expStackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_INT64: {
		int64 realValue = *intPtr;
		err = expStackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_FLOAT: {
		float realValue = *doublePtr;
		err = expStackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_DOUBLE: {
		double realValue = *doublePtr;
		err = expStackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	default:
		printm("invalid number\n");
		return -1;
	}

	return 0;
}
int expStackPushVar(BufferListInfo** stack, sqlDataType dataType, void* value, uint32 size) {
	if (stack == NULL || dataType == SQL_DATA_INVALID || value == NULL || size == 0)
		return -1;
	int err = 0;
	valueInfo type = { 0 };
	type.valueType = VT_var;
	type.dataType = dataType;

	err = expStackPush(stack, type, value, size); if (err < 0) { return -1; }

	return 0;
}
int expStackPushBinary(BufferListInfo** stack, sqlDataType dataType, void* value, uint32 size) {
	if (stack == NULL || dataType < SQL_DATA_STRING || value == NULL || size == 0)
		return -1;
	int err = 0;
	valueInfo type = { 0 };
	type.valueType = VT_binary;
	type.dataType = dataType;

	err = expStackPush(stack, type, value, size); if (err < 0) { return -1; }

	return 0;
}
int expStackPushString(BufferListInfo** stack, sqlDataType dataType, void* value, uint32 size) {
	if (stack == NULL || dataType != SQL_DATA_STRING || value == NULL || size == 0)
		return -1;
	int err = 0;
	err = expStackPushBinary(stack, dataType, value, size);

	return err;
}
int expStackPushBool(BufferListInfo** stack, uint8 isTrue) {
	if (stack == NULL)
		return -1;
	valueInfo type = { 0 };
	type.valueType = VT_Bool;
	type.isTrue = isTrue;
	int err = expStackPush(stack, type, NULL, 0);
	if (err < 0)
		return -1;

	return 0;
}
int expStackPushUnkownList(BufferListInfo** stack, BufferListInfo* unkownHead) {
	if (stack == NULL)
		return -1;
	int err = 0;
	valueInfo type = { 0 };
	type.valueType = VT_Unkown;

	err = expStackPush(stack, type, unkownHead, sizeof(BufferListInfo*)); if (err) { return -2; }

	return 0;
}

int hostCalculateNumberExpression(BufferListInfo** head) {
	//1+2 
	//no =
	//必须已知为number类型
	if (head == NULL)
		return -1;
	int err = 0;
	//printm("calculate single exp: "); stackShowByCnt(*head, 3);
	
	if (BuffListMgrGetListSize(*head) < 3)
		return 1;
	BufferListInfo* leftNum = (*head);
	BufferListInfo* op = leftNum->next;
	BufferListInfo* rightNum = op->next;
	sqlDataType dataType = 0;
	operatType opType = 0;
	valueInfo* leftInfo = leftNum->buff;
	valueInfo* rightInfo = rightNum->buff;
	valueInfo* opInfo = op->buff;
	if (leftInfo->valueType == VT_var || rightInfo->valueType == VT_var)
		return 1;
	if (leftInfo->valueType != VT_num || opInfo->valueType != VT_operator || rightInfo->valueType != VT_num)
		return -1;
	if (leftInfo->dataType != rightInfo->dataType)
		return -1;
	if (leftInfo->dataType < SQL_DATA_INT8 || leftInfo->dataType > SQL_DATA_DOUBLE)
		return -1;
	if (rightInfo->dataType < SQL_DATA_INT8 || rightInfo->dataType > SQL_DATA_DOUBLE)
		return -1;
	if (opInfo->opType < OT_Plus || opInfo->opType > OT_Division)
		return -1;

	dataType = leftInfo->dataType;
	opType = opInfo->opType;

	BufferListInfo* next = NULL;
	BufferListInfo* tmp = BuffListMgrGetNodeByIndex(*head, 2); if (!tmp) { return -1; }
	next = tmp->next;	tmp->next = NULL;

	rightNum = expStackPopTail(head);
	op = expStackPopTail(head);
	leftNum = expStackPopTail(head);

	switch (dataType)
	{
	case SQL_DATA_INT8:
	{
		if (opType == OT_Division && INT8_VALUE(rightInfo+1) == 0) {
			ERROR(-2);
		}
		int8_t value = calculateInt(INT8_VALUE(leftInfo+1), INT8_VALUE(rightInfo+1), opType);
		err = expStackPushNumber(head, dataType, &value);
		break;
	}
	case SQL_DATA_INT16:
	{
		if (opType == OT_Division && INT16_VALUE(rightInfo+1) == 0) {
			ERROR(-2);
		}
		int16_t value = calculateInt(INT16_VALUE(leftInfo+1), INT16_VALUE(rightInfo+1), opType);
		err = expStackPushNumber(head, dataType, &value);
		break;
	}
	case SQL_DATA_INT32:
	{
		if (opType == OT_Division && INT32_VALUE(rightInfo+1) == 0) {
			ERROR(-2);
		}
		int32_t value = calculateInt(INT32_VALUE(leftInfo+1), INT32_VALUE(rightInfo+1), opType);
		err = expStackPushNumber(head, dataType, &value);
		break;
	}
	case SQL_DATA_INT64:
	{
		if (opType == OT_Division && INT64_VALUE(rightInfo+1) == 0) {
			ERROR(-2);
		}
		int64_t value = calculateInt(INT64_VALUE(leftInfo+1), INT64_VALUE(rightInfo+1), opType);
		err = expStackPushNumber(head, dataType, &value);
		break;
	}
	case SQL_DATA_FLOAT:
	{
		if (opType == OT_Division && FLOAT_VALUE(rightInfo+1) == 0) {
			ERROR(-2);
		}
		float value = calculateInt(FLOAT_VALUE(leftInfo+1), FLOAT_VALUE(rightInfo+1), opType);
		err = expStackPushNumber(head, dataType, &value);
		break;
	}
	case SQL_DATA_DOUBLE:
	{
		if (opType == OT_Division && DOUBLE_VALUE(rightInfo+1) == 0) {
			ERROR(-2);
		}
		double value = calculateInt(DOUBLE_VALUE(leftInfo+1), DOUBLE_VALUE(rightInfo+1), opType);
		err = expStackPushNumber(head, dataType, &value);
		break;
	}
	default:
		ERROR(-2);
	}
	if (err < 0) { ERROR(-2); }


	BuffRelease(&rightNum);
	BuffRelease(&op);
	BuffRelease(&leftNum);

	(*head)->next = next;
	//printm("calculate done: "); stackShowByCnt(*head, 1);
	return 0;

error:
	BuffRelease(&rightNum);
	BuffRelease(&op);
	BuffRelease(&leftNum);

	printm("calculate fail\n");
	return -2;
}
int hostCalculateMultipleExpression(BufferListInfo** head, uint32 cnt, uint32* outCnt) {
	//a+(2+3)+3
	//no =
	if (head == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	if (BuffListMgrGetListSize(*head) < cnt)
		return -1;
	if (cnt < 3) {
		if (outCnt) {
			*outCnt = cnt;
		}
		return 0;
	}
	int err = 0;
	BufferListInfo* headTail = BuffListMgrGetTailNode(*head, cnt);
	BufferListInfo* exp = NULL;
	BufferListInfo* expTail = NULL;
	BufferListInfo* ptr = *head;
	uint8 hasCalculated = 0;
	uint32 expCnt = 0;

	for (uint32 i = 0; i < cnt; i++, ptr = ptr->next) {
		//stackShow(*head);
		//stackShow(ptr);
		//stackShow(exp);
		valueInfo* ptrInfo = ptr->buff;
		if (ptrInfo->valueType == VT_operator) {
			//计算前面的算式
			expCnt = BuffListMgrGetListSize(exp);
			while (expCnt >= 3) {
				BufferListInfo* leftLast = expCnt > 3 ? BuffListMgrGetNodeByIndex(exp, expCnt - 4) : NULL;
				BufferListInfo* leftNode = !leftLast ? BuffListMgrGetNodeByIndex(exp, expCnt - 3) : leftLast->next;
				BufferListInfo* opNode = leftNode->next;
				BufferListInfo* rightNode = opNode->next;
				valueInfo* leftLastInfo = leftLast?leftLast->buff:NULL;
				valueInfo* leftNodeInfo = leftNode->buff;
				valueInfo* opNodeInfo = opNode->buff;
				valueInfo* rightNodeInfo = rightNode->buff;

				if (leftNodeInfo->valueType != VT_num && leftNodeInfo->valueType != VT_var) {
					break;
				}
				if (rightNodeInfo->valueType != VT_num && rightNodeInfo->valueType != VT_var) {
					break;
				}

				if (OP_LEVEL(opNodeInfo->opType) < OP_LEVEL(ptrInfo->opType))
					break;
				
				err = hostCalculateNumberExpression(!leftLast ? &exp : &leftLast->next); if (err < 0) { ERROR(-1) }
				if (err == 1) {
					//var or cnt < 3
					break;
				}
				expCnt = BuffListMgrGetListSize(exp);
				hasCalculated = 1;
			}

		}

		err = expStackPush(&exp, *ptrInfo, ptrInfo+1, ptr->size - sizeof(valueInfo)); if (err < 0) { ERROR(-1) }

		if (i == cnt - 1) {
			//final
			//计算前面的算式
			uint32 expCnt = BuffListMgrGetListSize(exp);
			while (expCnt >= 3) {
				BufferListInfo* leftLast = expCnt > 3 ? BuffListMgrGetNodeByIndex(exp, expCnt - 4) : NULL;
				BufferListInfo* leftNode = !leftLast ? BuffListMgrGetNodeByIndex(exp, expCnt - 3) : leftLast->next;
				BufferListInfo* opNode = leftNode->next;
				BufferListInfo* rightNode = opNode->next;

				//valueInfo* leftLastInfo = NULL;
				//if (leftLast) {
				//	leftLastInfo = leftLast->buff;
				//}
				valueInfo* leftNodeInfo = leftNode->buff;
				valueInfo* opNodeInfo = opNode->buff;
				valueInfo* rightNodeInfo = rightNode->buff;

				if (leftNodeInfo->valueType != VT_num && leftNodeInfo->valueType != VT_var) {
					break;
				}
				if (rightNodeInfo->valueType != VT_num && rightNodeInfo->valueType != VT_var) {
					break;
				}

				if (OP_LEVEL(opNodeInfo->opType) < ptrInfo->opType)
					break;

				err = hostCalculateNumberExpression(!leftLast ? &exp : &leftLast->next); if (err < 0) { ERROR(-1) }
				if (err == 1) {
					//var or cnt < 3
					break;
				}
				expCnt = BuffListMgrGetListSize(exp);
				hasCalculated = 1;
			}
		}

		//(x)
		if (ptrInfo->opType == OT_Right_bracket) {
			expCnt = BuffListMgrGetListSize(exp);
			//stackShow(exp);
			if (expCnt >= 3) {
				BufferListInfo* left = BuffListMgrGetNodeByIndex(exp, expCnt - 3);
				BufferListInfo* mid = left->next;
				BufferListInfo* right = mid->next;
				valueInfo* leftInfo = left->buff;
				valueInfo* midInfo = mid->buff;
				valueInfo* rightInfo = right->buff;

				if (leftInfo->valueType == VT_operator && leftInfo->opType == OT_Left_bracket &&
					rightInfo->valueType == VT_operator && rightInfo->opType == OT_Right_bracket) {
					err = BuffListMgrRemoveNodeByPtr(&exp, left); if (err < 0) { ERROR(-1) }
					err = BuffListMgrRemoveNodeByPtr(&exp, right); if (err < 0) { ERROR(-1) }
				}
			}
		}
	}
	if (!exp)
		ERROR(-1)

		//stackShow(*head);
		if (hasCalculated == 0) {
			if (outCnt) {
				*outCnt = cnt;
			}
			return 0;
		}

	if (outCnt) {
		*outCnt = BuffListMgrGetListSize(exp);
	}

	expTail = exp; while (expTail->next) { expTail = expTail->next; } //exp tail
	
	//...6+3... -> ...9... and 6+3 than free 6+3
	BufferListInfo* oldExp = *head;
	expTail->next = headTail->next;
	headTail->next = NULL;
	*head = exp;

	//free 6+3
	freeExpStack(&oldExp);
	return 0;

error:
	BuffRelease(&exp);
	return -2;
}
int hostCalculateContinuousAndExpression(BufferListInfo** head, uint32 cnt, uint32* outCnt) {
	//t&t&(unkown)&t -> t&(unkown)
	//t&f&t -> fasle
	//t&(unkown)&t&t&(unkown)&t&t -> t&(unkown)
	//只计算单层
	if (head == NULL || outCnt == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	if (BuffListMgrGetListSize(*head) < cnt)
		return -1;

	int err = 0;
	uint8 retTrue = 0;
	uint8 findBool = 0;
	uint8 findUnknown = 0;
	BufferListInfo* last = NULL;
	BufferListInfo* ptr = *head;

	//is has false
	for (uint32 i = 0; i < cnt; i++, ptr = ptr->next) {
		valueInfo* ptrInfo = ptr->buff;
		if (ptrInfo->valueType == VT_Bool && ptrInfo->isTrue == 0) {
			//return false
			BufferListInfo* removingTail = BuffListMgrGetTailNode(*head, cnt);
			BufferListInfo* next = removingTail->next;
			removingTail->next = NULL;
			freeExpStack(head);
			err = expStackPushBool(head, FALSE); if (err < 0) { return -1; }
			(*head)->next = next;
			*outCnt = 1;
			return 2;
		}
		if (ptrInfo->valueType == VT_Unkown) {
			findUnknown = 1;
		}
	}

	if (findUnknown == 0) {
		//no false and no unkown
		//return true
		BufferListInfo* removingTail = BuffListMgrGetTailNode(*head, cnt);
		BufferListInfo* next = removingTail->next;
		removingTail->next = NULL;
		freeExpStack(head);
		err = expStackPushBool(head, TRUE); if (err < 0) { return -1; }
		(*head)->next = next;
		*outCnt = 1;
		return 1;
	}

	//all is true without unkown
	//简化unkown以外多余的bool, 只保留一个true和所有unkown
	findBool = 0;
	(*outCnt) = 0;
	for (uint32 i = 0; i < cnt && ptr; i++, (*outCnt)++) {
		valueInfo* ptrInfo = ptr->buff;
		if (ptrInfo->valueType == VT_operator) {
			ptr = ptr->next;
		}
		else if (ptrInfo->valueType == VT_Bool) {
			if (findBool) {
				BufferListInfo* tmp = ptr;
				ptr = ptr->next;
				err = BuffListMgrRemoveNodeByPtr(head, tmp); if (err < 0) { return -1; }
				i++;
				if (ptr) {
					tmp = ptr;
					ptr = ptr->next;
					err = BuffListMgrRemoveNodeByPtr(head, tmp); if (err < 0) { return -1; }
					i++;
				}
			}
			else {
				findBool = 1;
			}
		}
		else if (ptrInfo->valueType == VT_Unkown) {
			findUnknown = 1;
			ptr = ptr->next;
		}
		else {
			return -1;
		}
	}

	return 0;
}
int hostCalculateContinuousOrExpression(BufferListInfo** head, uint32 cnt, uint32* outCnt) {
	//f|t&(unkown)|f -> true
	//f|f|f -> fasle
	//只计算单层
	if (head == NULL || outCnt == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	if (BuffListMgrGetListSize(*head) < cnt)
		return -1;
	//stackShowByCnt(*head, cnt);
	int err = 0;
	uint8 retTrue = 0;
	uint8 findBool = 0;
	uint8 findUnknown = 0;
	BufferListInfo* last = NULL;
	BufferListInfo* ptr = *head;

	//is has true
	for (uint32 i = 0; i < cnt; i++, ptr = ptr->next) {
		valueInfo* ptrInfo = ptr->buff;
		if (ptrInfo->valueType == VT_Bool && ptrInfo->isTrue == 1) {
			//return true
			BufferListInfo* removingTail = BuffListMgrGetTailNode(*head, cnt);
			BufferListInfo* next = removingTail->next;
			removingTail->next = NULL;
			freeExpStack(head);
			err = expStackPushBool(head, TRUE); if (err < 0) { return -1; }
			(*head)->next = next;
			*outCnt = 1;
			return 1;
		}
		if (ptrInfo->valueType == VT_Unkown) {
			findUnknown = 1;
		}
	}

	if (findUnknown == 0) {
		//no true and no unkown
		//return false
		BufferListInfo* removingTail = BuffListMgrGetTailNode(*head, cnt);
		BufferListInfo* next = removingTail->next;
		removingTail->next = NULL;
		freeExpStack(head);
		err = expStackPushBool(head, FALSE); if (err < 0) { return -1; }
		(*head)->next = next;
		*outCnt = 1;
		return 2;
	}

	//stackShowByCnt(*head, cnt); printm("\n");
	//all is false without unkown
	//简化unkown以外多余的bool, 只保留一个false和所有unkown
	findBool = 0;
	(*outCnt) = 0;
	ptr = *head;
	for (uint32 i = 0; i < cnt && ptr; i++) {
		valueInfo* ptrInfo = ptr->buff;
		if (ptrInfo->valueType == VT_operator) {
			ptr = ptr->next;
			(*outCnt)++;
		}
		else if (ptrInfo->valueType == VT_Bool) {
			if (findBool) {
				BufferListInfo* tmp = ptr;
				ptr = ptr->next;
				err = BuffListMgrRemoveNodeByPtr(head, tmp); if (err < 0) { return -1; }
				i++;
				if (ptr) {
					tmp = ptr;
					ptr = ptr->next;
					err = BuffListMgrRemoveNodeByPtr(head, tmp); if (err < 0) { return -1; }
					i++;
				}
			}
			else {
				findBool = 1;
				(*outCnt)++;
			}
		}
		else if (ptrInfo->valueType == VT_Unkown) {
			findUnknown = 1;
			ptr = ptr->next;
			(*outCnt)++;
		}
		else {
			return -1;
		}
	}

	return 0;
}
int hostCalculateMultipleBoolExpression(BufferListInfo** head, uint32 cnt, uint32* outCnt) {
	//输入为已经由单表达式计算完毕的，只包含最简unkown exp和bool的表达式集合

	if (head == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	if (BuffListMgrGetListSize(*head) < cnt)
		return -1;
	//viewExpStack(*head); printm("\n");
	int err = 0;
	BufferListInfo* nextBracket = *head;
	BufferListInfo* nextBracketLast = NULL;
	BufferListInfo* nextAnd = *head;
	BufferListInfo* ptr = NULL;
	uint32 newTmpCnt = 0;
#define PTR (ptr?ptr:(*head))

	//判断当前层是否恒为True/False
	//and
	while (1) {
#define CNT (ptr?(BuffListMgrGetListSize(ptr)):cnt)

		BufferListInfo* nextAndList = NULL;
		BufferListInfo* nextAndListLast = NULL;
		uint32 nextAndListIndex = 0;
		uint32 nextAndListCnt = 0;
		//stackShow(PTR);
		//stackShowByCnt(PTR, CNT);
		nextAndList = hostGetNextContinuousAndExpression(PTR, CNT, &nextAndListCnt);
		if (nextAndList) {
			nextAndListIndex = BuffListMgrGetNodeIndex(PTR, CNT, nextAndList); if (nextAndListIndex == INVALID_BUFFER_LIST_NODE_INDEX) { return -1; }
			
			nextAndListLast = BuffListMgrGetLastNode(PTR, nextAndList);
			uint32 srcCnt = nextAndListCnt;
			
			err = hostCalculateContinuousAndExpression((nextAndListLast == NULL || nextAndListLast == *head) ? head : (&(nextAndListLast->next)), srcCnt, &newTmpCnt); if (err < 0) { return -1; }
			if (err == 1 || err == 2) {
				//已经移动过了
			}
			else {
				//unkown
			}
			if (newTmpCnt < srcCnt) {
				nextAndListCnt -= (srcCnt - newTmpCnt);
				cnt -= (srcCnt - newTmpCnt);
			}
		}
		else {
			break;
		}
		//viewExpStack(*head); printm("\n");
		if (BuffListMgrGetListSize(PTR) == 1) { break; }//bool
		ptr = BuffListMgrGetNodeByIndex(PTR, nextAndListIndex + nextAndListCnt); if (!ptr) { break; }
		//stackShow(PTR);
	}

	//viewExpStack(*head); printm("\n");
	//Or
	BufferListInfo* nextOr = expStackGetNextOperator(*head, cnt, OT_Or);
	if (nextOr) {
		err = hostCalculateContinuousOrExpression(head, cnt, &newTmpCnt); if (err < 0) { return -1; }
		if (err == 1 || err == 2) {
			if (outCnt)
				*outCnt = 1;
			return err;
		}
		else {
			//unkown
		}
		if (newTmpCnt < cnt)
			cnt = newTmpCnt;
	}
	//stackShow(*head);
	//viewExpStack(*head); printm("\n");

	if (BuffListMgrGetListSize(*head) == 1) { 
		valueInfo* resultInfo = (*head)->buff;
		ASSERT(resultInfo->valueType != VT_Bool, "not singal bool");
		if (resultInfo->isTrue) { return 1; }
		else { return 2; }
	}//bool

	//当前层无法计算，进一步计算下一层级
	uint8 isChangedUnkownList = 0;
	BufferListInfo* nextUnkown = NULL;
	uint32 nextUnkownIndex = 0;
	nextUnkown = expStackGetNextUnkownNode(*head, cnt);
	while (nextUnkown) {
		uint32 unkownListCnt = nextUnkown->size;
		uint32 unkownListNewCnt = 0;
		valueInfo* nextUnkownInfo = nextUnkown->buff;
		char* value = nextUnkownInfo + 1;
		err = hostCalculateMultipleBoolExpression(value, unkownListCnt, &unkownListNewCnt); if (err < 0) { return -1; }
		if (err == 1 || err == 2) {
			uint8 isTrue = err;
			freeExpStack(value);
			nextUnkownInfo->valueType = VT_Bool;
			nextUnkownInfo->isTrue = isTrue;
			isChangedUnkownList = 1;
		}
		else {
			//unkown			
		}

		nextUnkown = expStackGetNextUnkownNode(nextUnkown->next, cnt);
	}

	//需要重新判断当前层级，因为有unkown子层级可能被转化为bool
	if (isChangedUnkownList) {
		//and
		while (1) {
#define CNT (ptr?(cnt - nextAndListCnt - nextAndListIndex - 1):cnt)

			BufferListInfo* nextAndList = NULL;
			BufferListInfo* nextAndListLast = NULL;
			uint32 nextAndListIndex = 0;
			uint32 nextAndListCnt = 0;
			//stackShow(PTR);
			//stackShowByCnt(PTR, CNT);
			nextAndList = hostGetNextContinuousAndExpression(PTR, CNT, &nextAndListCnt);
			if (nextAndList) {
				err = stackGetNodeIndex(PTR, CNT, nextAndList, &nextAndListIndex); if (err < 0) { return -1; }
				nextAndListLast = BuffListMgrGetLastNode(PTR, nextAndList);
				uint32 srcCnt = nextAndListCnt;

				err = hostCalculateContinuousAndExpression((nextAndListLast == NULL || nextAndListLast == *head) ? head : (&(nextAndListLast->next)), srcCnt, &newTmpCnt); if (err < 0) { return -1; }
				if (err == 1 || err == 2) {
					//已经移动过了
				}
				else {
					//unkown
				}
				if (newTmpCnt < srcCnt) {
					if (nextAndListLast == NULL || nextAndListLast == *head) {
						cnt -= (srcCnt - newTmpCnt);
					}
					else {
						nextAndListCnt -= (srcCnt - newTmpCnt);
						cnt -= (srcCnt - newTmpCnt);
					}

				}
			}
			else {
				break;
			}
			ptr = BuffListMgrGetNodeByIndex(PTR, nextAndListIndex + nextAndListCnt - 1); if (!ptr) { return -1; }
			//stackShow(PTR);
		}

		//or 
		err = hostCalculateContinuousOrExpression(head, cnt, &newTmpCnt); if (err < 0) { return -1; }
		if (err == 1 || err == 2) {
			if (outCnt)
				*outCnt = 1;
			return err;
		}
		else {
			//unkown
		}

		if (newTmpCnt < cnt)
			cnt = newTmpCnt;
	}

	return 0;
}
int hostSingleExpressionToBool(BufferListInfo** head) {
	//1=1 -> true
	if (head == NULL)
		return -1;
	int err = 0;
	int ret = 0;
	if (BuffListMgrGetListSize(*head) < 3) {
		return -1;
	}
	//printm("single to bool: "); viewExpStack(*head); printm("\n");

	BufferListInfo* next = NULL;
	BufferListInfo* tmp = BuffListMgrGetNodeByIndex(*head, 2); if (!tmp) { return -1; }
	next = tmp->next;	tmp->next = NULL;

	BufferListInfo* rightNum = expStackPopTail(head);
	BufferListInfo* op = expStackPopTail(head);
	BufferListInfo* leftNum = expStackPopTail(head);
	valueInfo* rInfo = rightNum->buff;
	valueInfo* oInfo = op->buff;
	valueInfo* lInfo = leftNum->buff;
	sqlDataType dataType = lInfo->dataType;
	operatType opType = oInfo->opType;

	if (opType < OT_Greater_than || opType > OT_Equal) {
		ERROR(-1);
	}

	uint8 isTrue = 0;
	if (dataType >= SQL_DATA_INT8 && dataType <= SQL_DATA_TABLE) {
		int64_t leftValue_int = 0;
		int64_t rightValue_int = 0;
		switch (dataType)
		{
		case SQL_DATA_INT8:
		{
			leftValue_int = INT8_VALUE(lInfo+1);
			rightValue_int = INT8_VALUE(rInfo+1);
			break;
		}
		case SQL_DATA_INT16:
		{
			leftValue_int = INT16_VALUE(lInfo+1);
			rightValue_int = INT16_VALUE(rInfo+1);
			break;
		}
		case SQL_DATA_INT32:
		{
			leftValue_int = INT32_VALUE(lInfo+1);
			rightValue_int = INT32_VALUE(rInfo+1);
			break;
		}
		case SQL_DATA_INT64:
		{
			leftValue_int = INT64_VALUE(lInfo+1);
			rightValue_int = INT64_VALUE(rInfo+1);
			break;
		}
		default:
			ERROR(-1);
		}
	
		err = calculateIntEquation(leftValue_int, rightValue_int, opType); if (err < 0) { ERROR(-1); }
		if (err) {
			isTrue = 1;
			ret = 1;
		}
		else {
			isTrue = 0;
			ret = 0;
		}
		err = expStackPushBool(head, isTrue);
	}
	else if (dataType >= SQL_DATA_FLOAT && dataType <= SQL_DATA_DOUBLE) {
		double leftValue_double = 0;
		double rightValue_double = 0;

		switch (dataType)
		{
		case SQL_DATA_FLOAT:
		{
			leftValue_double = FLOAT_VALUE(lInfo+1);
			rightValue_double = FLOAT_VALUE(rInfo+1);
			break;
		}
		case SQL_DATA_DOUBLE:
		{
			leftValue_double = DOUBLE_VALUE(lInfo+1);
			rightValue_double = DOUBLE_VALUE(rInfo+1);
			break;
		}
		default:
			ERROR(-1);
		}

		err = calculateDoubleEquation(leftValue_double, rightValue_double, opType); if (err < 0) { ERROR(-1); }
		if (err) {
			isTrue = 1;
			ret = 1;
		}
		else {
			isTrue = 0;
			ret = 0;
		}
		err = expStackPushBool(head, isTrue);
	}
	else if (dataType >= SQL_DATA_STRING) {
		//string 
		uint32 leftSize = leftNum->buff + leftNum->size - (lInfo + 1);
		uint32 rightSize = rightNum->buff + rightNum->size - (rInfo + 1);
		err = calculateStringEquation(lInfo+1, leftSize, rInfo+1, rightSize, opType); if (err < 0) { ERROR(-1); }
		if (err) {
			isTrue = 1;
			ret = 1;
		}
		else {
			isTrue = 0;
			ret = 0;
		}
		err = expStackPushBool(head, isTrue);
	}
	else {
		ERROR(-1);
	}
	if (err < 0) { ERROR(-2); }

	BuffRelease(&rightNum);
	BuffRelease(&op);
	BuffRelease(&leftNum);

	(*head)->next = next;
	//printm("calculate done: "); stackShowByCnt(*head, 1);
	return ret;

error:
	BuffRelease(&rightNum);
	BuffRelease(&op);
	BuffRelease(&leftNum);
	printm("calculate fail\n");
	return -2;
}
int hostSingleExpressionSimplify(BufferListInfo** head, uint32 cnt, uint32* newCnt) {
	//return 0;无法化简结果 1:表达式为true 2:表达式为false
	//col3 * (4 - 1) > 8
	//(1 + 2) * 6 = 0
	//col1 + co2 > 60
	int err = 0;
	if (head == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	int ret = 0;	
	//printm("sub: "); viewExpStack(*head); printm("\n");
	uint32 equalIndex = 0;
	uint32 leftCnt = 0;
	uint32 rightCnt = 0;
	BufferListInfo* equal = hostGetNextEqualNode(*head, cnt, &equalIndex);
	if (!equal) { ERROR(-1); }

	leftCnt = equalIndex;
	rightCnt = cnt - leftCnt - 1;
	if (hostGetNextEqualNode(equal->next, leftCnt, NULL)) {
		// not subexpression
		printm("not subexpression\n");
		return NULL;
	}

	uint32 varCnt = 0;//varNodeCntInSubStack(head, cnt);
	if (!equal) { ERROR(-1) }

	//viewExpStack(*head);
	err = hostCalculateMultipleExpression(head, leftCnt, &leftCnt); if (err < 0) { ERROR(-1) }
	//viewExpStack(*head);
	err = hostCalculateMultipleExpression(&(equal->next), rightCnt, &rightCnt); if (err < 0) { ERROR(-1) }
	//viewExpStack(*head);

	if (varCnt == 0) {
		//(1 + 2) * 6 = 0
		//直接输出Ture or False
		valueInfo* headInfo = (*head)->buff;
		//去括号
		if (headInfo->valueType == VT_operator && headInfo->opType == OT_Left_bracket) {
			BufferListInfo* tail = *head; while (tail->next) { tail = tail->next; }
			if (headInfo->valueType != VT_operator && headInfo->opType != OT_Right_bracket) {
				ERROR(-3)
			}
			err = BuffListMgrRemoveNodeByPtr(head, *head); if (err < 0) { ERROR(-2) }
			err = BuffListMgrRemoveNodeByPtr(head, tail); if (err < 0) { ERROR(-2) }
		}

		BufferListInfo* leftNum = NULL;
		BufferListInfo* rightNum = NULL;
		BufferListInfo* op = NULL;

		leftNum = *head;
		op = leftNum->next;
		rightNum = op->next;
		valueInfo* lInfo = leftNum->buff;
		valueInfo* oInfo = op->buff;
		valueInfo* rInfo = rightNum->buff;
		if (oInfo->valueType != VT_operator || lInfo->valueType != rInfo->valueType)
			ERROR(-2);
		err = hostSingleExpressionToBool(head, 3); if (err < 0) { ERROR(-2); }
		valueInfo* retInfo = (*head)->buff;
		if (retInfo->isTrue) {
			ret = 1;
		}
		else {
			ret = 2;
		}

		if (newCnt)
			*newCnt = 1;
	}
	else {
		if (newCnt)
			*newCnt = leftCnt + rightCnt + 1;
		ret = 0;
	}

	//viewExpStack(*head); printm("\n");
	return ret;

error:
	return err;
}

uint32 hostGetEqualCnt(BufferListInfo* stack, uint32 cnt) {
	BufferListInfo* tmp = stack;
	uint32 ret = 0;
	for (uint32 i = 0; i < cnt && tmp; i++, tmp = tmp->next) {
		valueInfo* info = tmp->buff;
		if (info->opType >= OT_Greater_than && info->opType <= OT_Equal && info->valueType == VT_operator) {
			ret++;
		}
	}
	return ret;
}

static BufferListInfo* expStackGetNextOperator(BufferListInfo* stack, uint32 cnt, operatType opType) {
	BufferListInfo* ptr = stack;
	for (uint32 i = 0; i < cnt && ptr; i++, ptr = ptr->next) {
		valueInfo* info = ptr->buff;
		if (info->valueType != VT_operator)
			continue;
		if (info->opType == opType)
			return ptr;
	}

	return NULL;
}
static BufferListInfo* expStackGetNextBracket(BufferListInfo* stack, uint32 cnt, uint32* outCnt) {
	ASSERT(stack == NULL || outCnt == NULL, "invalid input");
	if (stack == NULL || outCnt == NULL)
		return NULL;
	int err = 0;
	BufferListInfo* left = NULL;
	BufferListInfo* right = NULL;
	BufferListInfo* ptr = NULL;
	uint32 leftCnt = 0;
	uint32 ptrIndex = 0;
	uint32 leftIndex = 0;
	uint32 rightIndex = 0;
	//stackShowByCnt(stack, cnt); printm("\n");
	
	left = expStackGetNextOperator(stack, cnt, OT_Left_bracket); if (!left) { return NULL; }
	leftIndex = BuffListMgrGetNodeIndex(stack, cnt, left); if (leftIndex == INVALID_BUFFER_LIST_NODE_INDEX) { return NULL; }
	//stackShow(left);
	right = expStackGetNextOperator(left, cnt - leftIndex + 1, OT_Right_bracket); if (!right) { return NULL; }
	ptr = left->next;
	ptrIndex = BuffListMgrGetNodeIndex(stack, cnt, ptr); if (ptrIndex == INVALID_BUFFER_LIST_NODE_INDEX) { return NULL; }
	while (right && ptrIndex < cnt && ptr) {
		ptr = expStackGetNextOperator(ptr, cnt, OT_Left_bracket); if (!left) { return NULL; }
		if (ptr) {
			ptrIndex = BuffListMgrGetNodeIndex(stack, cnt, ptr); if (ptrIndex == INVALID_BUFFER_LIST_NODE_INDEX) { return NULL; }
			if (ptrIndex < cnt) {
				rightIndex = BuffListMgrGetNodeIndex(stack, cnt, right); if (rightIndex == INVALID_BUFFER_LIST_NODE_INDEX) { return NULL; }
				if (rightIndex < ptrIndex) {
					break;
				}
				BufferListInfo* tmp = expStackGetNextOperator(right->next, cnt - rightIndex - 1, OT_Right_bracket);
				if (!tmp) {
					return NULL;
				}
				right = tmp;
			}
			else {
				break;
			}
		}
		else {
			break;
		}

		ptr = ptr->next;
	}

	rightIndex = BuffListMgrGetNodeIndex(stack, cnt, right); if (rightIndex == INVALID_BUFFER_LIST_NODE_INDEX) { return NULL; }
	leftIndex = BuffListMgrGetNodeIndex(stack, cnt, left); if (leftIndex == INVALID_BUFFER_LIST_NODE_INDEX) { return NULL; }
	*outCnt = rightIndex - leftIndex - 1;
	return left->next;
}
static BufferListInfo* expStackGetLastOperator(BufferListInfo* stack, uint32 cnt, stackNode* node, operatType opType) {
	BufferListInfo* ptr = node;
	for (uint32 i = 0; i < cnt && ptr; i++) {
		ptr = BuffListMgrGetLastNode(stack, ptr);
		valueInfo* info = ptr->buff;
		if (info->valueType != VT_operator)
			continue;
		if (info->opType == opType)
			return ptr;
	}

	return NULL;
}
static BufferListInfo* expStackGetLastAndOr(BufferListInfo* stack, uint32_t cnt, BufferListInfo* node) {
	if (stack == NULL || node == NULL || stack == node)
		return NULL;
	BufferListInfo* last = node;
	for (uint32_t i = 0; i < cnt; i++) {
		last = BuffListMgrGetLastNode(stack, last);
		if (last == NULL)
			break;
		valueInfo* lastInfo = last->buff;
		if (lastInfo->valueType == VT_operator && (lastInfo->opType == OT_And || lastInfo->opType == OT_Or))
			return last;

		if (last == node)
			break;
	}
	return NULL;
}
static BufferListInfo* expStackGetNextUnkownNode(BufferListInfo* head, uint32 cnt) {
	if (head == NULL)
		return NULL;
	BufferListInfo* next = head;
	for (uint32 i = 0; i < cnt && next; i++, next = next->next) {
		valueInfo* info = next->buff;
		if (info->valueType == VT_Unkown)
			return next;
	}
	return NULL;
}
static int hostCalculateExpStack(BufferListInfo** head, bool* result) {
	//root
	//hostSingleExpressionSimplify
	int err = 0;
	BufferListInfo* stack = NULL;
	BufferListInfo* stackTail = stack;
	BufferListInfo* ptr = *head;
	BufferListInfo* last = NULL;

#define NEW_STACK_NODE {\
	if(stackTail){stackTail->next = getBuffer(sizeof(valueInfo) + valueSize, BUFFER_POOL);}\
	else { stack = getBuffer(sizeof(valueInfo) + valueSize, BUFFER_POOL); stackTail = stack; }\
	}

	while (ptr) {
		valueInfo* ptrInfo = ptr->buff;
		char* ptrValue = ptrInfo + 1;

		valueInfo info = { 0 }; memcpy(&info, ptrInfo, sizeof(valueInfo));
		char* value = ptrValue;
		uint32 valueSize = ptr->buff + ptr->size - ptrValue;

		if (ptrInfo->valueType == VT_Unkown) {
			BufferListInfo** sub = ptrValue;
			bool subRet = FALSE;
			err = hostCalculateExpStack(sub, &subRet); if (err) { return -1; }
			info.valueType = VT_Bool;
			info.isTrue = subRet;
			value = NULL;
			valueSize = 0;
		}

		if (!last) {
			NEW_STACK_NODE;
			valueInfo* newNodeInfo = stackTail->buff;
			memcpy(newNodeInfo, &info, sizeof(info));
			memcpy(newNodeInfo+1, value, valueSize);
		}
		else {
			//err = hostSingleExpressionSimplify();
		}

		if (info.valueType == VT_num || info.valueType == VT_Bool || info.valueType == VT_binary) {
			last = ptr;
		}

		ptr = ptr->next;

	}


	return err;
error:

	return err;
}

static int hostCalculateAllSubExpToBool(BufferListInfo** head) {
	BufferListInfo* ptr = *head;

	while (ptr) {
		valueInfo* ptrInfo = ptr->buff;
		if (ptrInfo->valueType == VT_Unkown) {
			
		}
	}

	return 0;
}
static int hostCalculateExpStack2(BufferListInfo** head) {
	BufferListInfo* ptr = *head;
	uint32 cnt = BuffListMgrGetListSize(*head);
	
	//remove unkown in cur level
	while (ptr) {
		BufferListInfo* nextUnkown = expStackGetNextUnkownNode(*head, cnt);
	}

	return 0;
}
static BufferListInfo* copyExpStack(BufferListInfo* head) {
	int err = 0;
	BufferListInfo* ret = NULL;
	BufferListInfo* retPtr = ret;
	BufferListInfo* tmp = head;
#define GET_SUB_EXP(info) (*((BufferListInfo**)(info + 1)))
	while (tmp) {
		valueInfo* info = tmp->buff;
		valueInfo* curInfo = NULL;
		if (!retPtr) {
			ret = getBuffer(tmp->size, BUFFER_POOL); if (!ret) { return -2; }
			retPtr = ret;
		}
		else {
			retPtr->next = getBuffer(tmp->size, BUFFER_POOL); if (!ret) { return -2; }
			retPtr = retPtr->next;
		}
		curInfo = retPtr->buff;
		
		if (info->valueType == VT_Unkown) {
			BufferListInfo* subExp = GET_SUB_EXP(info);
			BufferListInfo* sub2 = copyExpStack(subExp);
			memcpy(curInfo + 1, &sub2, sizeof(BufferListInfo*));
			memcpy(curInfo, info, sizeof(valueInfo));
		}
		else {
			memcpy(retPtr->buff, tmp->buff, tmp->size);
		}
		tmp = tmp->next;
	}

	return ret;
}
static bool calculateExpression(uint64 tableId, char* row, BufferListInfo** exp, SqlHandleGetColumnValueByNameCB callback) {
	int err = 0;
	//copy list
	bool ret = FALSE;
	//viewExpStack(*exp); printm("\n");
	BufferListInfo* tmp = copyExpStack(*exp);
	uint64 expCnt = BuffListMgrGetListSize(tmp);
	
	//viewExpStack(tmp); printm("\n");
	//exchange var node to real value
	err = scanExpStackAndExchangeVarNode(tableId, row, &tmp, callback); if (err<0) { return FALSE; }
	//viewExpStack(tmp); printm("\n");

	if (err) {
		//has bool multi exp
		expCnt = BuffListMgrGetListSize(tmp);
		err = hostCalculateMultipleBoolExpression(&tmp, expCnt, &expCnt); if (err <= 0) { return FALSE; }
		ret = err == 1 ? TRUE : FALSE;
	}
	else {
		//single exp -> 123 = 123
		err = hostSingleExpressionToBool(&tmp);
		ret = err == 1 ? TRUE : FALSE;
	}
	

	freeExpStack(&tmp);
	return ret;
}

static BufferListInfo* hostGetNextEqualNode(BufferListInfo* stack, uint32 cnt, uint32* index) {
	if (stack == NULL)
		return NULL;
	BufferListInfo* tmp = stack;
	uint32 i = 0;
	for (; i < cnt && tmp != NULL; i++, tmp = tmp->next) {
		valueInfo* info = tmp->buff;
		valueType valuType = BIT_AND(info->valueType, VALUE_INFO_VALUETYPE_BIT);
		operatType opType = BIT_AND(info->opType, VALUE_INFO_OPTYPE_BIT);
		if (valuType == VT_operator && opType >= OT_Greater_than && opType <= OT_Equal) {
			if (index)
				*index = i;
			return tmp;
		}
	}
	return NULL;
}
static BufferListInfo* hostGetLastAndOr(BufferListInfo* stack, uint32 cnt, BufferListInfo* node) {
	if (stack == NULL || node == NULL || stack == node)
		return NULL;
	BufferListInfo* last = node;
	for (uint32 i = 0; i < cnt; i++) {
		last = BuffListMgrGetLastNode(stack, last);
		if (last == NULL)
			break;
		valueInfo* info = last->buff;
		if (info->valueType == VT_operator && (info->opType == OT_And || info->opType == OT_Or))
			return last;

		if (last == node)
			break;
	}
	return NULL;
}
static BufferListInfo* hostGetNextAndOr(BufferListInfo* stack, uint32 cnt) {
	if (stack == NULL)
		return NULL;
	BufferListInfo* next = stack->next;
	for (uint32 i = 0; i < cnt && next; i++, next = next->next) {
		valueInfo* info = next->buff;
		if (info->valueType == VT_operator && (info->opType == OT_And || info->opType == OT_Or))
			return next;
	}
	return NULL;
}
static BufferListInfo* hostGetNextContinuousAndExpression(BufferListInfo* stack, uint32 cnt, uint32* outCnt) {
	//获取下一个连续的AND表达式组合
	//a|b&c&d|f -> b&c&d
	if (stack == NULL || !outCnt)
		return NULL;

	//stackShowByCnt(stack, cnt);
	int err = 0;

	BufferListInfo* start = NULL;
	BufferListInfo* end = NULL;
	uint32 startIndex = 0;
	uint32 endIndex = 0;
	//viewExpStack(stack); printm("\n");
	start = expStackGetNextOperator(stack, cnt, OT_And); if (!start) { return NULL; }
	
	start = expStackGetLastAndOr(stack, cnt, start);
	if (!start) {
		start = stack;
		startIndex = 0;
	}
	else {
		start = start->next;
	}
	startIndex = BuffListMgrGetNodeIndex(stack, cnt, start); if (startIndex == INVALID_BUFFER_LIST_NODE_INDEX) { return NULL; }
	end = expStackGetNextOperator(start, cnt - startIndex - 1, OT_Or); 
	if (end) {
		end = BuffListMgrGetLastNode(stack, end);
	}
	else {
		end = BuffListMgrGetTailNode(stack, cnt);
	}
	endIndex = BuffListMgrGetNodeIndex(stack, cnt, end); if (endIndex == INVALID_BUFFER_LIST_NODE_INDEX) { return NULL; }

	*outCnt = endIndex - startIndex + 1;
	//stackShowByCnt(start, *outCnt);

	return start;
}
static BufferListInfo* hostGetNextBracketMultipleExpressionInStack(BufferListInfo* head, uint32 cnt, uint32* outCnt) {
	//(...=... & ... > ...)

	if (head == NULL || outCnt == NULL)
		return NULL;
	int err = 0;
	uint32 bracketLen = 0;
	stackNode* ptr = NULL;
	stackNode* bracket = expStackGetNextBracket(head, cnt, &bracketLen);
	if (!bracket)
		return NULL;
	//is multiple exp?

	ptr = expStackGetNextOperator(bracket, bracketLen, OT_And);
	if (!ptr) {
		ptr = expStackGetNextOperator(bracket, bracketLen, OT_Or);
		if (!ptr) {
			return NULL;
		}
	}

	*outCnt = bracketLen;
	return bracket;
}

int SqlHandleDecodeStream(char* stream, uint64 size, SqlTokenHandle* tk) {
	return decodeStream(stream, size, tk);
}
void SqlHandleViewTokens(SqlTokenHandle* tk) {
	return viewSqlTokenHandle(tk);
}
void SqlHandleFreeTokens(SqlTokenHandle* tk) {
	return freeSqlTokenHandle(tk);
}
bool SqlHandleCalculateExpression(uint64 tableId, char* row, BufferListInfo** exp, SqlHandleGetColumnValueByNameCB callback) {
	return calculateExpression(tableId, row, exp, callback);
}
int SqlHandleCalculateSetExpression(uint64 tableId, char* row, BufferListInfo** exp, BufferListInfo** destCol, BufferListInfo** value, SqlHandleGetColumnValueByNameCB callback) {
	int err = 0;

	*destCol = *exp;
	BufferListInfo* equal = (*exp)->next;
	valueInfo* info = equal->buff;
	if (info->valueType != VT_operator || info->opType != OT_Equal) {
		return -1;
	}
	(*value) = equal->next;
	uint32 cnt = BuffListMgrGetListSize(*value);
	err = scanExpStackAndExchangeVarNode(tableId, row, value, callback); if (err < 0) { return FALSE; }
	equal->next = *value;
	viewExpStack(*exp); printm("\n");
	err = hostCalculateMultipleExpression(value, cnt, &cnt);
	if (err || cnt != 1) {
		return -2;
	}
	equal->next = *value;
	//viewExpStack(*exp); printm("\n");
	return 0;
}
void SqlHandleViewExpStack(BufferListInfo* head) {
	return viewExpStack(head);
}

extern int sqlCmdByShell(char* sql, uint32 sqlLen, char** stream, uint32* streamSize);
extern void showSqlStream(char* stream, uint32 size);

int test_getColValueCB(uint64 tableId, char* row, char* columnName, BufferListInfo** outValue) {
	char userName[128] = "zbc";
	char userName2[128] = "!@#";
	uint32 id = 143589;
	BufferListInfo* value = NULL;
	if (strncmp("userName", columnName, strlen(userName)) == 0) {
		value = getBuffer(sizeof(valueInfo) + strlen(userName), BUFFER_POOL);
		valueInfo* info = value->buff;
		info->valueType = VT_binary;
		info->dataType = SQL_DATA_STRING;
		memcpy(info+1, userName, strlen(userName));
	}
	else if (strncmp("username2", columnName, strlen(userName2)) == 0) {
		value = getBuffer(sizeof(valueInfo) + strlen(userName2), BUFFER_POOL);
		valueInfo* info = value->buff;
		info->valueType = VT_binary;
		info->dataType = SQL_DATA_STRING;
		memcpy(info + 1, userName2, strlen(userName2));
	}
	else if (strncmp("id", columnName, strlen("id")) == 0) {
		value = getBuffer(sizeof(valueInfo) + sizeof(id), BUFFER_POOL);
		valueInfo* info = value->buff;
		info->valueType = VT_num;
		info->dataType = SQL_DATA_INT32;
		memcpy(info + 1, &id, sizeof(id));
	}
	else {
		return -1;
	}
	
	*outValue = value;

	return 0;
}

void sqlCommandHandle_test1() {
	int err = 0;
	initBufferManagserBySize(1, BUFFER_POOL_NORMAL, BYTE_1G);

	char select[256] = "select col1,col2 from table1 where userName < 'str2' & ('col2' < 'col3' | id = 6 + 3)";//"select col1,col2 from table1 where userName < 'st r2' & (id = 6 | username2 = '123')";//"select col1,col2 from table1 where userName < 'str2' & ('col2' < 'col3' | id = 6)";
	char create[256] = "create table table1 ( col1 int8 index, col2 string(20), col3 float )";
	char update[256] = "update table1 set id = id+id2, id2 = 25 where id > id2";
	char insert[256] = "insert into table1 (userName, id,point) value ('str1', 123, 3.14)";
	char delete[256] = "delete from table2 where id > 80";
	char drop[256] = "drop table table1";

	char* cmdStr = select;
	SqlTokenHandle cmd = { 0 };
	char* sqlStream = NULL;
	uint32 sqlStreamLen = 0;
	err = sqlCmdByShell(cmdStr, strlen(cmdStr), &sqlStream, &sqlStreamLen);
	if (err) {
		printm("make stream fail\n"); return;
	}
	showSqlStream(sqlStream, sqlStreamLen); printm("\n");
	err = decodeStream(sqlStream, sqlStreamLen, &cmd); if (err) { printm("decode fail\n"); return; }
	free(sqlStream); sqlStream = NULL;
	viewSqlTokenHandle(&cmd);
	DEBUG_PrintBuffer();
	freeSqlTokenHandle(&cmd);
	DEBUG_PrintBuffer();
	printm("pass\n");
	return;
}
void sqlCommandHandle_test2() {
	int err = 0;
	initBufferManagserBySize(1, BUFFER_POOL_NORMAL, BYTE_1G);

	char select[256] = "select col1,col2 from table1 where userName < 'st r2' & (id = 6 | username2 = '123')";//"select col1,col2 from table1 where userName < 'st r2' & (id = 6 | username2 = '123')";//"select col1,col2 from table1 where userName < 'str2' & ('col2' < 'col3' | id = 6)";
	char create[256] = "create table table1 ( col1 int8 index, col2 string(20), col3 float )";
	char update[256] = "update table1 set id = id+id2, id2 = 25 where id > id2";
	char insert[256] = "insert into table1 (userName, id,point) value ('str1', 123, 3.14)";
	char delete[256] = "delete from table2 where id > 80";
	char drop[256] = "drop table table1";

	char* cmdStr = select;
	SqlTokenHandle cmd = { 0 };
	char* sqlStream = NULL;
	uint32 sqlStreamLen = 0;
	err = sqlCmdByShell(cmdStr, strlen(cmdStr), &sqlStream, &sqlStreamLen);
	if (err) {
		printm("make stream fail\n"); return;
	}
	showSqlStream(sqlStream, sqlStreamLen); printm("\n");

	err = decodeStream(sqlStream, sqlStreamLen, &cmd); if (err) { printm("decode fail\n"); return; }
	free(sqlStream); sqlStream = NULL;
	//viewSqlTokenHandle(&cmd);
	//DEBUG_PrintBuffer();
	bool res = calculateExpression(0, NULL, &cmd.whereStack, test_getColValueCB);
	printm("ret: %u\n", res);
	//DEBUG_PrintBuffer();
	freeSqlTokenHandle(&cmd);
	//DEBUG_PrintBuffer();

}