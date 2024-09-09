#include "myOperation.h"
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include "myStack.h"
#include <math.h>

#define DEBUG_OP

#define BIT_AND(value, bit) (value & (0xffffffffu >> (32-bit)))
#define VALUE_TYPE(info) (BIT_AND(info->valueType, VALUE_INFO_VALUETYPE_BIT))
#define OP_TYPE(info) (BIT_AND(info->opType, VALUE_INFO_OPTYPE_BIT))
#define DATA_TYPE(info) (BIT_AND(info->dataType, VALUE_INFO_DATATYPE_BIT))
#define BOOL(info) (BIT_AND(info->dataType, VALUE_INFO_DATATYPE_BIT))

#define ERROR(errCode) {err = errCode;goto error;}
#define STACK_NODE_OP_VALUE(node) (node->type.opType)
#define DOUBLE_VALUE(ptr) (*((double*)(ptr)))
#define FLOAT_VALUE(ptr) (*((float*)(ptr)))
#define INT8_VALUE(ptr) (*((int8_t*)(ptr)))
#define INT16_VALUE(ptr) (*((int16_t*)(ptr)))
#define INT32_VALUE(ptr) (*((int32_t*)(ptr)))
#define INT64_VALUE(ptr) (*((int64_t*)(ptr)))
#define BOOL_VALUE(ptr) (ptr->type.isTrue)
#define OP_LEVEL(op) (opLevel[op])
#define INVALID_COL_ID (UINT32_MAX)
#define RANGE_ITEM_LIST_BLOCK_SIZE (10U)

static const uint32_t opLevel[OT_MAX] = { 0, 2, 2, 3, 3, 1, 1, 1, 1, 1, 0, 0, 0, 0 };

extern sqlDataType getColType(char* tableName, char* colName);
extern sqlDataType str2DataType(char* str, uint32_t len, uint32_t* stringLen);

uint32_t getOpLevel(operatType op) {
	if (op >= OT_MAX - 1)
		return 0;

	return opLevel[op];
}
uint32_t getNextSubStrLenBeforeComma(char* str, uint32_t len) {
	//,
	if (str == NULL)
		return UINT32_MAX;
	char* ret = NULL;
	char* comma = strchr(str, ',');
	if (comma == NULL || comma - str > len) {
		return len;
	}
	else {
		return comma - str;
	}
}
uint32_t getNextSubStrLenBeforeSpace(char* str, uint32_t len) {
	//' '
	if (str == NULL)
		return UINT32_MAX;
	char* ret = NULL;
	char* comma = strchr(str, ' ');
	if (comma == NULL || comma - str > len) {
		return len;
	}
	else {
		return comma - str;
	}
}
char* getNextNotSpaceChar(char* str, uint32_t len) {
	if (str == NULL)
		return NULL;

	for (uint32_t i = 0; i < len; i++) {
		if (str[i] != ' ')
			return str + i;
	}
	return NULL;
}

uint8_t isValidOperator(char* op) {
	if (op == NULL)
		return 0;
	switch (*op)
	{
	case '+':
	case '-':
	case '*':
	case '/':
	case '>':
	case '<':
	case '=':
	case '(':
	case ')':
	case '&':
	case '|':
		return 1;
	default:
		break;
	}
	return 0;
}
uint8_t isValidNumber(char* num, uint32_t len) {
	//0: not number; 1: int, 2: double
	if (num == NULL || len == 0)
		return 0;
	char* ptr = num;
	uint8_t ret = 1;
	uint32_t dotCnt = 0;
	if (ptr[0] == '-') {
		if (len == 1)
			return 0;
		ptr++;
	}
	else if (ptr[0] == '.') {
		return 0;
	}
	while (ptr-num < len) {
		if (*ptr == '.') {
			if (dotCnt > 0)
				return 0;
			ret = 2;
			dotCnt++;
		}
		else if (*ptr < '0' || *ptr > '9')
			return 0;
		ptr++;
	}
	return ret;
}
uint8_t isValidColName(char* colName, uint32_t len) {
	if (colName == NULL)
		return 0;
	char* ptr = colName;
	/*
			48 49 50 51 52 53 54 55 56 57 	(0~9)
			65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90		(A~Z)
			95	(_)
			97 98 99 100 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116 117 118 119 120 121 122 (a~z)
			*/
	for (uint32_t i = 0; i < len; i++) {
		for (char j = 48; j <= 122; j++) {
			if (j > 57 && j < 65) {
				j = 65;
			}
			else if (j > 90 && j < 95) {
				j = 95;
			}
			else if (j > 95 && j < 97) {
				j = 97;
			}
			else;

			if (colName[i] == j) {
				return 1;
			}
		}
	}

	return 0;
}
uint8_t isValidSetExpression(char* exp, uint32_t len) {
	if (!exp) { return 0; }
	char* ptr = exp;
	char* equal = NULL;
	equal = strchr(exp, '=');
	if (!equal)
		return 0;
	if (equal - exp >= len)
		return 0;
	
	ptr = exp;
	for (;ptr<equal;ptr++) {
		if (isValidOperator(ptr))
			return 0;

	}
	ptr = equal + 1;
	for (; ptr < exp+len; ptr++) {
		if (*ptr == '>' || *ptr == '<' || *ptr == '=')
			return 0;
	}

	return 1;

}
int getCurDoubleStr(char* str, uint32_t* doubleStrlen, double* outNum) {
	if (!str || !doubleStrlen)
		return -1;
	int err = 0;
	uint32_t cnt = strlen(str);
	char* ptr = str;
	uint8_t isMinus = 0;
	uint8_t leftNum = 0;
	uint8_t dot = 0;
	uint8_t rightNum = 0;
	if (ptr[0] == '-') {
		isMinus = 1; ptr++; cnt--;
	}
	
	for (uint32_t i = isMinus; i < cnt;i++) {
		if (str[i] == '.') {
			dot++;
			if (dot > 1)
				return -1;
		}
		else if (str[i] >= '0' && str[i] <= '9') {
			if (dot > 0) {
				rightNum++;
			}
			else {
				leftNum++;
			}
		}
		else {
			//cur is operator
			break;
		}

		if (dot > 0 && leftNum > 0 && rightNum > 0 || dot == 0 && leftNum > 0 && rightNum == 0) {
			//valid double number
			*doubleStrlen = i + 1;
		}
	}

	if (outNum) {
		err = str2Double(str, *doubleStrlen, NULL, outNum); if (err < 0) { return 0; }
	}

	return leftNum>0?0:-1;
}
int getCurIntStr(char* str, uint32_t* intStrlen, int64_t* outNum) {
	if (!str || !intStrlen)
		return -1;
	int err = 0;
	uint32_t cnt = strlen(str);
	char* ptr = str;
	uint8_t isMinus = 0;
	uint8_t leftNum = 0;
	uint8_t dot = 0;
	uint8_t rightNum = 0;
	if (ptr[0] == '-') { isMinus = 1; ptr++; }

	for (uint32_t i = isMinus; i < cnt; i++) {
		if (str[i] == '.') {
			return -1;
		}
		else if (str[i] >= '0' && str[i] <= '9') {
			if (dot > 0) {
				rightNum++;
			}
			else {
				leftNum++;
			}
		}
		else {
			if (dot == 0 && leftNum > 0 && rightNum == 0) {
				//valid int number
				break;
			}
			else {
				//cur is operator
				return -1;
			}
		}
	}
	*intStrlen = leftNum;
	if (outNum) {
		err = str2Int64(str, *intStrlen, NULL, outNum); if (err < 0) { return 0; }
	}

	return 0;
}
int getCurStringStr(char* str, char** outPtr, uint32_t* outLen) {
	if (!str || !outPtr || !outLen)
		return -1;
	int err = 0;
	uint32_t cnt = strlen(str);
	*outPtr = getNextSingleQuotes(str, cnt, outLen);

	return 0;
}
int getCurVarStr(char* str, char** outPtr, uint32_t* outLen) {
	if (!str || !outPtr || !outLen)
		return -1;
	uint32_t cnt = strlen(str);
	char* op = getNextOperation(str, cnt);
	char* space = strchr(str, ' ');
	char* comma = strchr(str, ',');
	*outPtr = str;

	if (op && (op < space || !space) && (op < comma || !comma)) {
		*outLen = op - str;
	}
	else if (space && (space < op || !op) && (space < comma ||!comma)) {
		*outLen = space - str;
	}
	else if (comma && (comma < op || !op) && (comma < space || !space)) {
		*outLen = comma - str;
	}
	else {
		*outLen = cnt;
	}

	return 0;
}
operatType getCurStrOperatorType(char* str, uint32_t* opLen) {
	if (!str || !opLen)
		return OT_Invalid;
	operatType type = OT_Invalid;
	switch (str[0])
	{
	case '+':
		type = OT_Plus;
		break;
	case '-':
		type = OT_Minus;
		break;
	case '*':
		type = OT_Multiplication;
		break;
	case '/':
		type = OT_Division;
		break;
	case '>':
		if (str[1] == '=') {
			type = OT_Greater_than_or_equal;
			(*opLen)++;
		}
		else
			type = OT_Greater_than;
		break;
	case '<':
		if (str[1] == '=') {
			type = OT_Less_than_or_equal;
			(*opLen)++;
		}
		else
			type = OT_Less_than;
		break;
	case '=':
		type = OT_Equal;
		break;
	case '(':
		type = OT_Left_bracket;
		break;
	case ')':
		type = OT_Right_bracket;
		break;
	case '&':
		type = OT_And;
		break;
	case '|':
		type = OT_Or;
		break;
	default:
		return OT_Invalid;
	}
	(*opLen)++;

	return type;
}
char* getNextOperation(char* exp, uint32_t len) {
	if (exp == NULL) {
		return NULL;
	}

	char* ptr = exp;
	char op[] = {
	'+',
	'-',
	'*',
	'/',
	'>',
	'<',
	'=',
	'&',
	'|',
	'(',
	')'
	};
	for (uint32_t i = 0; i < len; i++, ptr++) {
		for (uint32_t j = 0; j < sizeof(op); j++) {
			if (*ptr == op[j]) {
				return ptr;
			}
		}
	}

	return NULL;
}
char* getNextUnkowns(char* exp, uint32_t len, uint32_t* outLen) {
	if (!exp || !outLen)
		return NULL;
	char* ptr = exp;
	char* nextOp = NULL;
	while (ptr -exp <= len) {
		nextOp = getNextOperation(ptr, len-(ptr-exp));
		if (nextOp) {
			if (isValidNumber(ptr, nextOp - ptr) == 0) {
				// not number
				*outLen = nextOp - ptr;
				return ptr;
			}
		}
		else {
			//final
			if (isValidNumber(ptr, len - (ptr - exp)) == 0) {
				*outLen = len - (ptr - exp);
				return ptr;
			}
			break;
		}
		ptr = nextOp+1;
	}

	return NULL;
}
char* getNextSingleQuotes(char* str, uint32_t len, uint32_t* outLen) {
	//单引号
	if (str == NULL || outLen == NULL)
		return NULL;
	char* ret = NULL;
	char* ptr = strchr(str, '\'');	if (!ptr) { return NULL; } if (ptr - str > len) { return NULL; }
	ret = ptr+1;
	ptr++;
	ptr = strchr(ptr, '\'');	if (!ptr) { return NULL; } if (ptr - str > len) { return NULL; }
	*outLen = ptr - ret;
	return ret;
}
char* getNexBracket(char* str, uint32_t len, uint32_t* outLen) {
	if (str == NULL || outLen == NULL)
		return NULL;
	char* left = NULL;
	char* right = NULL;
	char* ptr = NULL;
	uint32_t leftCnt = 0;

	left = strchr(str, '(');	if (!left) { return NULL; }	if (left - str > len) { return NULL; }
	right = strchr(left, ')');	if (!right) { return NULL; }	if (right - str > len) { return NULL; }
	ptr = left + 1;
	while (right && ptr - str < len && ptr) {
		ptr = strchr(ptr, '(');
		if (ptr) {
			if (ptr - str < len) {
				char* tmp = strchr(right + 1, ')');	if (!right) { return NULL; }	if (right - str > len) { return NULL; } if (right < ptr) { break; }
				right = tmp;
			}
			else {
				break;
			}
		}
		else {
			break;
		}

		ptr++;
	}
	*outLen = right - left - 1;
	return left + 1;
}
char* getNextBracketMultipleExpression(char* str, uint32_t len, uint32_t* outLen) {
	//(...=... & ... > ...)

	if (str == NULL || outLen == NULL)
		return NULL;
	int err = 0;
	uint32_t bracketLen = 0;
	char* ptr = NULL;
	char* bracket = getNexBracket(str, len, &bracketLen);
	if (!bracket)
		return NULL;
	//is multiple exp?
	ptr = strchr(bracket, '&');
	if (!ptr || ptr - bracket >= bracketLen) {
		ptr = strchr(bracket, '|');
		if (!ptr || ptr - bracket >= bracketLen) {
			return NULL;
		}
	}

	*outLen = bracketLen;
	return bracket;
}
stackNode* stackGetNextOperator(stackNode* stack, uint32_t cnt, operatType opType) {
	stackNode* ptr = stack;
	for (uint32_t i = 0; i < cnt && ptr;i++, ptr = ptr->next) {
		if (ptr->type.valueType != VT_operator)
			continue;
		if (ptr->type.opType == opType)
			return ptr;
	}

	return NULL;
}
stackNode* stackGetNextBracket(stackNode* stack, uint32_t cnt, uint32_t* outCnt) {
	if (stack == NULL || outCnt == NULL)
		return NULL;
	int err = 0;
	stackNode* left = NULL;
	stackNode* right = NULL;
	stackNode* ptr = NULL;
	uint32_t leftCnt = 0;
	uint32_t ptrIndex = 0;
	uint32_t leftIndex = 0;
	uint32_t rightIndex = 0;
	//stackShowByCnt(stack, cnt); printf("\n");
	left = stackGetNextOperator(stack, cnt, OT_Left_bracket); if (!left) { return NULL; }
	err = stackGetNodeIndex(stack, cnt, left, &leftIndex); if (err < 0) { return NULL; }
	//stackShow(left);
	right = stackGetNextOperator(left, cnt - leftIndex + 1, OT_Right_bracket); if (!right) { return NULL; }
	ptr = left->next;
	err = stackGetNodeIndex(stack, cnt, ptr, &ptrIndex); if (err < 0) { return NULL; }
	while (right && ptrIndex < cnt && ptr) {
		ptr = stackGetNextOperator(ptr, cnt, OT_Left_bracket); if (!left) { return NULL; }
		if (ptr) {
			err = stackGetNodeIndex(stack, cnt, ptr, &ptrIndex); if (err < 0) { return NULL; }
			if (ptrIndex < cnt) {
				err = stackGetNodeIndex(stack, cnt, right, &rightIndex); if (err < 0) { return NULL; }
				if (rightIndex < ptrIndex) {
					break;
				}
				stackNode* tmp = stackGetNextOperator(right->next, cnt - rightIndex - 1, OT_Right_bracket); 
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
	err = stackGetNodeIndex(stack, cnt, right, &rightIndex); if (err < 0) { return NULL; }
	err = stackGetNodeIndex(stack, cnt, left, &leftIndex); if (err < 0) { return NULL; }
	*outCnt = rightIndex - leftIndex - 1;
	return left->next;
}
stackNode* stackGetLastOperator(stackNode* stack, uint32_t cnt, stackNode* node, operatType opType) {
	stackNode* ptr = node;
	for (uint32_t i = 0; i < cnt && ptr; i++) {
		ptr = stackGetLastNode(stack, ptr);
		valueInfo* info = &(ptr->type);
		if (info->valueType != VT_operator)
			continue;
		if (info->opType == opType)
			return ptr;
	}

	return NULL;
}
stackNode* stackGetLastAndOr(stackNode* stack, uint32_t cnt, stackNode* node, uint32_t* outCnt) {
	if (!stack || !node)
		return NULL;
	int err = 0;
	stackNode* lastAnd = NULL;
	stackNode* lastOr = NULL;
	uint32_t lastAndIndex = 0;
	uint32_t lastOrIndex = 0;
	uint32_t nodeIndex = 0;
	lastAnd = stackGetLastOperator(stack, cnt, node, OT_And);
	lastOr = stackGetLastOperator(stack, cnt, node, OT_Or);

	err = stackGetNodeIndex(stack, cnt, node, &nodeIndex); if (err < 0) { return NULL; }

	if (lastAnd)
		err = stackGetNodeIndex(stack, cnt, lastAnd, &lastAndIndex);
	else
		lastAndIndex = UINT32_MAX;

	if (lastOr)
		err = stackGetNodeIndex(stack, cnt, lastOr, &lastOrIndex);
	else
		lastOrIndex = UINT32_MAX;

	if (lastAndIndex < lastOrIndex) {
		if (outCnt) {
			*outCnt = nodeIndex - lastAndIndex;
		}
		return lastAnd;
	}
	else if (lastAndIndex > lastOrIndex) {
		if (outCnt) {
			*outCnt = nodeIndex - lastOrIndex;
		}
		return lastOr;
	}
	else {
		return NULL;
	}
}
stackNode* getNextUnkownNodeInStack(stackNode* head, uint32_t cnt) {
	if (head == NULL)
		return NULL;
	stackNode* next = head;
	for (uint32_t i = 0; i < cnt && next; i++, next = next->next) {
		if (next->type.valueType == VT_Unkown)
			return next;
	}
	return NULL;
}


sqlDataType getUnkownsDataTypeInExp(char* tableName, char* exp, uint32_t len){
	//遍历该算式中所有未知数，检查各自type
	if (!exp)
		return SQL_DATA_INVALID;
	int err = 0;
	char* ptr = exp;
	uint32_t unkownsLen = 0;
	char* unkowns = NULL;
	sqlDataType lastColDataType = SQL_DATA_INVALID;
	uint8_t hasNumber = 0;
	uint8_t hasString = 0;
	while (ptr-exp < len) {
#define LEN (len - (ptr-exp))
		ptr = getNextNotSpaceChar(ptr, LEN);
		if (!ptr) { break; }
		uint32_t curLen = 0;
		if (getCurDoubleStr(ptr, &curLen, NULL) == 0) {
			//number
			hasNumber = 1;
		}
		else if (getCurStrOperatorType(ptr, &curLen) != OT_Invalid) {

		}
		else if (ptr[0] == '\'') {
			char* strTmp = NULL;
			err = getCurStringStr(ptr, &strTmp, &curLen);
			hasString = 1;
			curLen += 2;
		}
		else {
			//unkown
			char* var = NULL;
			err = getCurVarStr(ptr, &var, &curLen);
			if (!isValidColName(var, curLen)) { return SQL_DATA_INVALID; }
			sqlDataType curType = getColType(tableName, var, curLen);
			
			if (curType != lastColDataType && lastColDataType != SQL_DATA_INVALID || curType == SQL_DATA_INVALID) {
				printf("ERR 不统一的算子类型: %.*s\n", len, exp);
				return SQL_DATA_INVALID;
			}
			lastColDataType = curType;
			if (curType >= SQL_DATA_STRING && hasNumber) {
				printf("ERR 二进制类型不允许存在数字: %.*s\n", len, exp);
				return SQL_DATA_INVALID;
			}
			else if(curType < SQL_DATA_STRING && hasString){
				printf("ERR 数字类型不允许存在字符串: %.*s\n", len, exp);
				return SQL_DATA_INVALID;
			}
		}
		
		ptr += curLen;
	}

	if (lastColDataType == SQL_DATA_INVALID) {
		if (hasString) {
			return SQL_DATA_STRING;
		}
		else {
			return SQL_DATA_DOUBLE;
		}
	}
	else {
		return lastColDataType;
	}
}

void showStackNode(stackNode* node, char* value) {
	if (node == NULL)
		return;
	if (value != NULL) {
		node->value = value;
	}
	valueInfo* info = &(node->type);
	switch (info->valueType)
	{
	case VT_operator: {
		uint8_t ot = info->opType & (0xfu);
		switch (ot)
		{
		case OT_Plus:
			printf("+");
			break;
		case OT_Minus:
			printf("-");
			break;
		case OT_Multiplication:
			printf("*");
			break;
		case OT_Division:
			printf("/");
			break;
		case OT_Greater_than:
			printf(">");
			break;
		case OT_Less_than:
			printf("<");
			break;
		case OT_Greater_than_or_equal:
			printf(">=");
			break;
		case OT_Less_than_or_equal:
			printf("<=");
			break;
		case OT_Equal:
			printf("=");
			break;
		case OT_Or:
			printf("|");
			break;
		case OT_And:
			printf("&");
			break;
		case OT_Left_bracket:
			printf("(");
			break;
		case OT_Right_bracket:
			printf(")");
			break;
		default:
			printf("[invalid]");
			return;
		}
	}
		break;
	case VT_var:
		printf("%.*s", node->size, node->value);
		break;
	case VT_num: {
		uint32_t type = info->dataType & 0xfu;
		switch (type)
		{
		case SQL_DATA_INT8:
			printf("%d", *((int8_t*)(node->value)));
			break;
		case SQL_DATA_INT16:
			printf("%d", *((int16_t*)(node->value)));
			break;
		case SQL_DATA_INT32:
			printf("%d", *((int32_t*)(node->value)));
			break;
		case SQL_DATA_INT64:
			printf("%lld", *((int64_t*)(node->value)));
			break;
		case SQL_DATA_FLOAT:
			printf("%g", *((float*)(node->value)));
			break;
		case SQL_DATA_DOUBLE:
			printf("%g", *((double*)(node->value)));
			break;
		case SQL_DATA_TABLE:
			printf("%u", *((uint32_t*)(node->value)));
			break;
		default:
			printf("[invalid]");
			return;
		}
		break;
	}
	case VT_binary:
		printf("\'%.*s\'", node->size, node->value);
		break;
	case VT_Bool:
		if (info->isTrue & 0x1u) {
			printf("true");
		}
		else {
			printf("false");
		}
		break;
	case VT_Unkown:
		printf("[unkown]");
		break;
	default:
		printf("[invalid]");
		break;
	}
}
void showStackBuff(char* buff, uint32_t size) {
	if (buff == NULL)
		return;

	stackNode* node = buff;
	char* ptr = node + 1;
	uint32_t unkownCnt = 0;
	uint32_t unkownIndex = 0;
	while (ptr - buff <= size) {
		if (node->type.valueType == VT_Unkown) {
			if (unkownCnt == 0) {
				unkownCnt = node->size;
			}
			node++;
		}
		else {
			showStackNode(node, ptr);
			if (unkownCnt > 0) { 
				unkownIndex++; 
				if (unkownCnt == unkownIndex) {
					unkownCnt = 0;
					unkownIndex = 0;
				}
			}
			node = ptr + node->size;
		}
		ptr = node + 1;
	}
	//printf("\n");
}
void stackShowByCnt(stackNode* stack, uint32_t cnt) {
	uint32_t realCnt = stackNodeCnt(stack);
	if (cnt > realCnt)
		return;
	stackNode* tmp = stack;
	for (realCnt = 0; realCnt < cnt; realCnt++) {
		if (tmp->type.valueType == VT_Unkown) {
			stackShowByCnt(tmp->value, tmp->size);
		}
		else {
			showStackNode(tmp, NULL);
		}
		
		tmp = tmp->next;
	}
	return;
}
void stackShow(stackNode* stack) {
	stackNode* tmp = stack;
	uint32_t cnt = 0;
	while (tmp != NULL) {
		showStackNode(tmp, NULL);
		cnt++;
		tmp = tmp->next;
		//printf(" ");
	}
	if (cnt == 0) {
		printf("空栈");
	}

	printf("\n");
}

int stackPushOperatior(stackNode** stack, operatType operationType) {
	if (stack == NULL || operationType <= OT_Invalid || operationType >= OT_MAX)
		return -1;
	valueInfo info = {0};
	info.valueType = VT_operator;
	info.opType = operationType;
	int err = stackPush(stack, info, NULL, 0);
	if (err < 0)
		return -2;

	return 0;
}
int stackPushNumber(stackNode** stack, sqlDataType dataType, void* value) {
	if (stack == NULL || dataType < SQL_DATA_INT8 || dataType > SQL_DATA_DOUBLE || value == NULL)
		return -1;
	int err = 0;
	valueInfo type = {0};
	type.valueType = VT_num;
	type.dataType = dataType;
	double* doublePtr = value;
	int64_t* intPtr = value;
	switch (dataType)
	{
	case SQL_DATA_INT8: {
		int8_t realValue = *intPtr;
		err = stackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_INT16: {
		int16_t realValue = *intPtr;
		err = stackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_INT32: {
		int32_t realValue = *intPtr;
		err = stackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_INT64: {
		int64_t realValue = *intPtr;
		err = stackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_FLOAT: {
		float realValue = *doublePtr;
		err = stackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_DOUBLE: {
		double realValue = *doublePtr;
		err = stackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	case SQL_DATA_TABLE: {
		uint32_t realValue = *intPtr;
		err = stackPush(stack, type, &realValue, sizeof(realValue)); if (err < 0) { return -1; }
		break;
	}
	default:
		printf("invalid number\n");
		return -1;
	}

	return 0;
}
int stackPushVar(stackNode** stack, sqlDataType dataType, void* value, uint32_t size) {
	if (stack == NULL || dataType == SQL_DATA_INVALID || value == NULL || size == 0)
		return -1;
	int err = 0;
	valueInfo type = {0};
	type.valueType = VT_var;
	type.dataType = dataType;

	err = stackPush(stack, type, value, size); if (err < 0) { return -1; }

	return 0;
}
int stackPushBinary(stackNode** stack, sqlDataType dataType, void* value, uint32_t size) {
	if (stack == NULL || dataType < SQL_DATA_STRING || value == NULL || size == 0)
		return -1;
	int err = 0;
	valueInfo type = {0};
	type.valueType = VT_binary;
	type.dataType = dataType;

	err = stackPush(stack, type, value, size); if (err < 0) { return -1; }

	return 0;
}
int stackPushString(stackNode** stack, sqlDataType dataType, void* value, uint32_t size) {
	if (stack == NULL || dataType != SQL_DATA_STRING || value == NULL || size == 0)
		return -1;
	int err = 0;
	err = stackPushBinary(stack, dataType, value, size);

	return err;
}
int stackPushBool(stackNode** stack, uint8_t isTrue) {
	if (stack == NULL)
		return -1;
	valueInfo type = {0};
	type.valueType = VT_Bool;
	type.isTrue = isTrue;
	int err = stackPush(stack, type, NULL, 0);
	if (err < 0)
		return -1;

	return 0;
}
int stackPushUnkownList(stackNode** stack, stackNode* unkownHead, uint32_t unKownCnt) {
	if (stack == NULL || unKownCnt == 0)
		return -1;
	int err = 0;
	valueInfo type = { 0 };
	type.valueType = VT_Unkown;

	stackNode* tail = NULL;
	if (*stack == NULL) {
		*stack = malloc(sizeof(stackNode)); if ((*stack) == NULL) { return -1; }
		tail = *stack;
		
	}
	else {
		tail = *stack; while (tail->next) { tail = tail->next; }
		tail->next = malloc(sizeof(stackNode)); if (tail->next == NULL) { return -1; }
		tail = tail->next;
	}
	memset(tail, 0, sizeof(stackNode));
	tail->type.valueType = VT_Unkown;
	tail->value = unkownHead;
	tail->size = unKownCnt;
	
	return 0;
}
void stackFreeOperatorStack(stackNode** stack) {
	if (stack == NULL)
		return;
	if (*stack == NULL)
		return;
	int err = 0;
	uint32_t cnt = stackNodeCnt(*stack);
	//printf("free: "); stackShow(*stack);
	//free unkown list
	stackNode* nextUnkown = NULL;
	uint32_t nextUnkownIndex = 0;
	nextUnkown = getNextUnkownNodeInStack(*stack, cnt);//??? not first?
	while (nextUnkown) {
		stackFreeOperatorStack(&(nextUnkown->value));
		cnt = stackNodeCnt(nextUnkown->next);
		nextUnkown = getNextUnkownNodeInStack(nextUnkown->next, cnt);
	}

	stackFreeStack(stack);
	return;
}

int calculateInt(int num1, int num2, operatType op) {
	int ret = 0;
	switch (op)
	{
	case OT_Plus:
		ret = num1 + num2;
		break;
	case OT_Minus:
		ret = num1 - num2;
		break;
	case OT_Multiplication:
		ret = num1 * num2;
		break;
	case OT_Division:
		ret = num1 / num2;
		break;
	default:
		printf("invalid calculate\n");
		break;
	}

	return ret;
}
int calculateIntEquation(int64_t num1, int64_t num2, operatType op) {
	if (op < OT_Greater_than || op > OT_Equal) {
		//(~,>) | (=, ~)
		return -1;
	}
	switch (op)
	{
	case OT_Greater_than:
		return num1 > num2;
	case OT_Less_than:
		return num1 < num2;
	case OT_Greater_than_or_equal:
		return num1 >= num2;
	case OT_Less_than_or_equal:
		return num1 <= num2;
	case OT_Equal:
		return num1 == num2;
	default:
		break;
	}

	return -1;
}
int calculateDoubleEquation(double num1, double num2, operatType op) {
	if (op < OT_Greater_than || op > OT_Equal) {
		//(~,>) | (=, ~)
		return -1;
	}
	switch (op)
	{
	case OT_Greater_than:
		return num1 > num2;
	case OT_Less_than:
		return num1 < num2;
	case OT_Greater_than_or_equal:
		return num1 >= num2;
	case OT_Less_than_or_equal:
		return num1 <= num2;
	case OT_Equal:
		return num1 == num2;
	default:
		break;
	}

	return -1;
}
int calculateBinaryEquation(char* binary1, uint32_t len1, char* binary2, uint32_t len2, operatType op) {
	//原理是从左到右遇到的第一个不同的字节，谁大算谁大
	//0: false, 1:true, -1:error
	if (binary1 == NULL || binary2 == NULL)
		return -1;
	if (op < OT_Greater_than || op > OT_Equal) {
		//(~,>) | (=, ~)
		return -1;
	}
	int ret = 0;
	int result = 0;
	if (len1 == len2) {
		result = memcmp(binary1, binary2, len1);
	}
	else {
		uint32_t size = len1 > len2 ? len1 : len2;
		char* buff = malloc(size); if (buff == NULL) { return -2; } memset(buff, 0, size);
		if (len1 > len2) {
			memcpy(buff, binary2, len2);
			result = memcmp(binary1, buff, size);
			
		}
		else {
			memcpy(buff, binary1, len1);
			result = memcmp(buff, binary2, size);
		}
	}

	if (result > 0 && op == OT_Greater_than) {
		//binary1 > binary2
		ret = 1;
	}
	else if (result < 0 && op == OT_Less_than) {
		ret = 1;
	}
	else if (result == 0 && (op == OT_Equal || op == OT_Greater_than_or_equal || op == OT_Less_than_or_equal)) {
		ret = 1;
	}
	else {
		ret = 0;
	}
	return ret;
}
int calculateStringEquation(char* str1, uint32_t len1, char* str2, uint32_t len2, operatType op) {
	int err = 0;
	err = calculateBinaryEquation(str1, len1, str2, len2, op);
	return err;
}
unsigned int calculateBoolEquation(unsigned int bool1, unsigned int bool2, operatType op) {
	if (op != OT_Or && op != OT_And) {
		return -1;
	}
	switch (op)
	{
	case OT_Or:
		return bool1 || bool2;
	case OT_And:
		return bool1 && bool2;
	default:
		break;
	}

	return -1;
}
uint32_t getEqualCnt(stackNode* stack, uint32_t cnt) {
	stackNode* tmp = stack;
	uint32_t ret = 0;
	for (uint32_t i = 0; i < cnt && tmp; i++, tmp = tmp->next) {
		valueInfo* info = &(tmp->type);
		if (info->opType >= OT_Greater_than && info->opType <= OT_Equal && info->valueType == VT_operator) {
			ret++;
		}
	}
	return ret;
}
stackNode* getNextEqualNode(stackNode* stack, uint32_t cnt, uint32_t* index) {
	if (stack == NULL)
		return NULL;
	stackNode* tmp = stack;
	uint32_t i = 0;
	for (; i < cnt && tmp != NULL; i++, tmp = tmp->next) {
		valueInfo* info = &(tmp->type);
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
stackNode* getLastAndOr(stackNode* stack, uint32_t cnt, stackNode* node) {
	if (stack == NULL || node == NULL || stack == node)
		return NULL;
	stackNode* last = node;
	for (uint32_t i = 0; i < cnt; i++) {
		last = stackGetLastNode(stack, last);
		if (last == NULL)
			break;
		if (last->type.valueType == VT_operator && (last->type.opType == OT_And || last->type.opType == OT_Or))
			return last;

		if (last == node)
			break;
	}
	return NULL;
}
stackNode* getNextAndOr(stackNode* stack, uint32_t cnt) {
	if (stack == NULL)
		return NULL;
	stackNode* next = stack->next;
	for (uint32_t i = 0; i < cnt && next; i++, next = next->next) {
		if (next->type.valueType == VT_operator && (next->type.opType == OT_And || next->type.opType == OT_Or))
			return next;
	}
	return NULL;
}
stackNode* getNextContinuousAndExpression(stackNode* stack, uint32_t cnt, uint32_t* outCnt) {
	//获取下一个连续的AND表达式组合
	//a|b&c&d|f -> b&c&d
	if (stack == NULL || !outCnt)
		return NULL;

	//stackShowByCnt(stack, cnt);
	int err = 0;

	stackNode* start = NULL;
	stackNode* end = NULL;
	uint32_t startIndex = 0;
	uint32_t endIndex = 0;
	start = stackGetNextOperator(stack, cnt, OT_And); if (start == NULL) { return NULL; }
	start = getLastAndOr(stack, cnt, start); 
	if (!start) {
		start = stack; 
		startIndex = 0;
	}
	else {
		start = start->next;
	}
	err = stackGetNodeIndex(stack, cnt, start, &startIndex); if (err < 0) { return NULL; }

	end = stackGetNextOperator(start, cnt - startIndex - 1, OT_Or); if (start == NULL) { return NULL; }
	if (end) {
		end = stackGetLastNode(stack,end);
	}
	else {
		end = stackGetTailNode(stack, cnt);
	}
	err = stackGetNodeIndex(stack, cnt, end, &endIndex); if (err < 0) { return NULL; }

	*outCnt = endIndex - startIndex+1;
	//stackShowByCnt(start, *outCnt);

	return start;
}
stackNode* getNextBracketMultipleExpressionInStack(stackNode* head, uint32_t cnt, uint32_t* outCnt) {
	//(...=... & ... > ...)

	if (head == NULL || outCnt == NULL)
		return NULL;
	int err = 0;
	uint32_t bracketLen = 0;
	stackNode* ptr = NULL;
	stackNode* bracket = stackGetNextBracket(head, cnt, &bracketLen);
	if (!bracket)
		return NULL;
	//is multiple exp?
	ptr = stackGetNextOperator(bracket, bracketLen, OT_And);
	if (!ptr) {
		ptr = stackGetNextOperator(bracket, bracketLen, OT_Or);
		if (!ptr) {
			return NULL;
		}
	}

	*outCnt = bracketLen;
	return bracket;
}

uint32_t varNodeCntInSubStack(stackNode** head, uint32_t cnt) {
	if (head == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	stackNode* tmp = *head;
	uint32_t varNodeCnt = 0;
	valueInfo* info = NULL;

	for (uint32_t i = 0; i < cnt; i++) {
		info = &(tmp->type);
		if (tmp == NULL)
			return UINT32_MAX;
		if (VALUE_TYPE(info) == VT_var)
			varNodeCnt++;
		tmp = tmp->next;
	}

	return varNodeCnt;
}
int removeUnuselessBracketsInStr(char* str, uint32_t len, uint32_t* newLen) {
	if (!str)
		return -1;
	if (strlen(str) < len)
		return -1;
	uint32_t outLen = len;
	char* ptr = str;

	//(((exp)))
	while (ptr && ptr - str < len) {
		uint32_t bracketLen = 0;
		char* nextBracket = getNexBracket(ptr, outLen -(ptr-str), &bracketLen);
		if (!nextBracket)
			break;
		if (*(nextBracket - 1) == '(' && *(nextBracket+ bracketLen + 1) == ')') {
			uint32_t remainSize = outLen - (nextBracket + bracketLen + 1 - str);
			memmove(nextBracket-1, nextBracket, bracketLen);
			memmove(nextBracket-1 + bracketLen, nextBracket + bracketLen+1, remainSize);
			outLen -= 2;
			ptr = nextBracket - 1;
		}
		else {
			ptr = nextBracket;
		}
	}


	if (outLen < len) {
		memset(str+outLen, 0, len-outLen);
	}
	if (newLen)
		*newLen = outLen;
	return 0;
}
int removeUnuselessBracketsInStack(stackNode** head) {
	if (head == NULL)
		return -1;
	if (*head == NULL)
		return -1;

	int err = 0;
	uint32_t cnt = stackNodeCnt(*head);
	stackNode* ptr = *head;
	stackNode* last = NULL;
	stackNode* next = NULL;
	//(x)
	ptr = (*head)->next;
	while (ptr != NULL) {
		uint8_t removed = 0;
		if (ptr->type.valueType == VT_operator && (STACK_NODE_OP_VALUE(ptr) == OT_Left_bracket || STACK_NODE_OP_VALUE(ptr) == OT_Right_bracket)) {
			ptr = ptr->next;
			continue;
		}
		last = stackGetLastNode(*head, ptr);
		next = ptr->next;
		if (last && next) {
			if (last->type.valueType == VT_operator && next->type.valueType == VT_operator
				&& STACK_NODE_OP_VALUE(last) == OT_Left_bracket && STACK_NODE_OP_VALUE(next) == OT_Right_bracket
				) {

				err = stackRemoveNodeByPtr(head, last); if (err < 0) { return -1; }
				err = stackRemoveNodeByPtr(head, next); if (err < 0) { return -1; }
				cnt -= 2;
				removed = 1;
			}
		}

		if (!removed)
			ptr = ptr->next;
	}
	//stackShow(*head);
	//(((x)))
	ptr = *head;
	stackNode* nextBracket = NULL;
	while (ptr != NULL) {
		//stackShow(ptr);
		uint32_t bracketCnt = 0;
		uint32_t rightIndex = 0;
		uint8_t removed = 0;
		uint32_t ptrIndex = 0;
		err = stackGetNodeIndex(*head, cnt, ptr, &ptrIndex); if (err < 0) { return -1; }
		nextBracket = getNextBracketMultipleExpressionInStack(ptr, cnt - ptrIndex, &bracketCnt);
		if (!nextBracket)
			break;
		//stackShowByCnt(nextBracket, bracketCnt);
		err = stackGetNodeIndex(*head, cnt, nextBracket, &rightIndex); if (err < 0) { return -1; }
		last = stackGetLastNode(*head, stackGetLastNode(*head, nextBracket));
		next = stackGetNodeByIndex(*head, rightIndex + bracketCnt);
		if (last && next) {
			if (last->type.valueType == VT_operator && next->type.valueType == VT_operator
				&& STACK_NODE_OP_VALUE(last) == OT_Left_bracket && STACK_NODE_OP_VALUE(next) == OT_Right_bracket
				) {

				err = stackRemoveNodeByPtr(head, last); if (err < 0) { return -1; }
				err = stackRemoveNodeByPtr(head, next); if (err < 0) { return -1; }
				cnt -= 2;
				removed = 1;

				//stackShow(*head);
			}
		}

		//ptr = ptr->next; //if unremoved
		ptr = nextBracket;
	}

	return 0;
}
double calculateDouble(double num1, double num2, operatType op) {
	double ret = 0;
	switch (op)
	{
	case OT_Plus:
		ret = num1 + num2;
		break;
	case OT_Minus:
		ret = num1 - num2;
		break;
	case OT_Multiplication:
		ret = num1 * num2;
		break;
	case OT_Division:
		ret = num1 / num2;
		break;
	default:
		printf("invalid calculate\n");
		break;
	}

	return ret;
}
int str2Double(char* str, uint32_t len, uint8_t* isMinus, double* outNum) {
	if (str == NULL || outNum == NULL)
		return -1;
	uint8_t loc_isMinuse = 0;
	*outNum = 0;

	char* ptr = str;
	if (*ptr == '-') {
		loc_isMinuse = 1;
		if(isMinus)
			*isMinus = 1;
		ptr++;
		len--;
	}
	uint8_t hasDot = 0;
	uint8_t dotNumCnt = 0;
	for (uint32_t i = 0; i < len; i++, ptr++) {
		if (*ptr == '.') {
			if (i == 0) {
				return -1;
			}
			if (hasDot > 0)
				return -1;
			hasDot++;
			continue;
		}
		if (*ptr < '0' || *ptr > '9') {
			return -1;
		}
		if (hasDot) {
			dotNumCnt++;
			*outNum += (*ptr - '0') * pow(10, dotNumCnt * -1);
		}
		else {
			*outNum *= 10;
			*outNum += *ptr - '0';
		}
	}
	if (loc_isMinuse)
		*outNum *= -1;

	return 0;
}
int str2Int64(char* str, uint32_t len, uint8_t* isMinus, int64_t* outNum) {
	if (str == NULL || outNum == NULL)
		return -1;
	*outNum = 0;
	uint8_t loc_isMinuse = 0;

	char* dot = strchr(str, '.');
	if (dot) {
		if (dot - str < len)
			return -1;
	}

	char* ptr = str;
	if (*ptr == '-') {
		loc_isMinuse = 1;
		if(isMinus)
			*isMinus = 1;
		ptr++;
		len--;
	}

	for (uint32_t i = 0; i < len; i++, ptr++) {

		if (*ptr < '0' || *ptr > '9') {
			return -1;
		}

		*outNum *= 10;
		*outNum += *ptr - '0';
	}
	if (loc_isMinuse)
		*outNum *= -1;

	return 0;
}

int strTo8BitNumBuffer(char* str, uint32_t len, sqlDataType type, uint8_t* isMinus, char outNum[8]) {
	if (str == NULL || type == SQL_DATA_INVALID)
		return -1;
	uint8_t loc_isMinuse = 0;
	memset(outNum, 0, 8);
	switch (type)
	{
	case SQL_DATA_INT8:
	{
		int8_t* value = outNum;
		break;
	}
	case SQL_DATA_INT16:
	{
		int8_t* value = outNum;
		break;
	}
	case SQL_DATA_INT32:
	{
		int8_t* value = outNum;
		break;
	}
	case SQL_DATA_INT64:
	{
		int8_t* value = outNum;
		break;
	}
	case SQL_DATA_TABLE: 
	{
		// int
		int64_t* value = outNum;

		break;
	}
	case SQL_DATA_FLOAT:
	{
		int8_t* value = outNum;
		break;
	}
	case SQL_DATA_DOUBLE:
	{
		//float
		double* value = outNum;

		break;
	}

	default:
		break;
	}


	char* ptr = str;
	if (*ptr == '-') {
		*isMinus = 1;
		ptr++;
		len--;
	}
	uint8_t hasDot = 0;
	uint8_t dotNumCnt = 0;
	for (uint32_t i = 0; i < len; i++, ptr++) {
		if (*ptr == '.') {
			if (i == 0) {
				return -1;
			}
			if (hasDot > 0)
				return -1;
			hasDot++;
			continue;
		}
		if (*ptr < '0' || *ptr > '9') {
			return 1;
		}
		if (hasDot) {
			dotNumCnt++;
			*outNum += (*ptr - '0') * pow(10, dotNumCnt * -1);
		}
		else {
			*outNum *= 10;
			*outNum += *ptr - '0';
		}
	}
	if (*isMinus)
		*outNum *= -1;

	return 0;
}

int sqlStrSingleExpressionToStack(char* tableName, char* exp, uint32_t len, stackNode** head) {
	if (exp == NULL || head == NULL)
		return -1;

	int err = 0;

	//printf("single exp: %.*s\n", len, exp);
	char* ptr = exp;

	sqlDataType expDataType = getUnkownsDataTypeInExp(tableName, exp, len);
	if (expDataType == SQL_DATA_INVALID)
		return -1;
	bool hasOpWithoutEqual = FALSE;
	bool hasStringType = FALSE;
	while (ptr-exp < len && ptr) {
#define LEN (len - (ptr-exp))
		uint8_t isMinus = 0;
		char* next = getNextNotSpaceChar(ptr, LEN);
		uint32_t nextLen = 0;
		if (expDataType >= SQL_DATA_INT8 && expDataType <= SQL_DATA_INT64) {
			int64_t num = 0;
			err = getCurIntStr(next, &nextLen, &num);
			if (err == 0) {
				//cur is num
				err = stackPushNumber(head, expDataType, &num); if (err < 0) { return -1; }
			}
			else {
				//cur is var
				char* var = NULL;
				err = getCurVarStr(next, &var, &nextLen); if (err < 0) { return -1; }
				err = stackPushVar(head, expDataType, var, nextLen); if (err < 0) { return -1; }
			}
		}
		else if (expDataType >= SQL_DATA_FLOAT && expDataType <= SQL_DATA_DOUBLE) {
			double num = 0;
			err = getCurDoubleStr(next, &nextLen, &num);
			if (err == 0) {
				//cur is num
				err = stackPushNumber(head, expDataType, &num); if (err < 0) { return -1; }
			}
			else {
				//cur is var
				char* var = NULL;
				err = getCurVarStr(next, &var, &nextLen); if (err < 0) { return -1; }
				err = stackPushVar(head, expDataType, var, nextLen); if (err < 0) { return -1; }
			}
		}
		else if (expDataType == SQL_DATA_STRING) {
			//string
			hasStringType = TRUE;
			char* strValue = NULL;
			if (next[0] == '\'') {
				err = getCurStringStr(next, &strValue, &nextLen); if (err < 0) { return -1; }
				err = stackPushString(head, expDataType, strValue, nextLen); if (err < 0) { return -1; }
				nextLen += 2;//单引号
			}
			else {
				//cur is var
				char* var = NULL;
				err = getCurVarStr(next, &var, &nextLen); if (err < 0) { return -1; }
				err = stackPushVar(head, expDataType, var, nextLen); if (err < 0) { return -1; }
			}
			
		}
		else {
			return -1;
		}

		ptr = next + nextLen;
		char* nextOp = getNextNotSpaceChar(ptr, LEN);
		//op
		if (nextOp) {
			if (!isValidOperator(nextOp)) { return -1; }
			uint32_t opLen = 0;
			operatType type = getCurStrOperatorType(nextOp, &opLen);
			if (type >= OT_Plus && type <= OT_Division) {
				hasOpWithoutEqual = TRUE;
			}
			err = stackPushOperatior(head, type);
			
			ptr = nextOp + opLen;
		}
		else {
			//final
			break;
		}
	}
	if (hasOpWithoutEqual && hasStringType) {
		//there are +/-/*// operator in string type exp
		return -1;
	}
	return 0;
}
int sqlStrExpressionToStack(char* tableName, char* str, uint32_t len, stackNode** head) {
	if (str == NULL || head == NULL)
		return -1;

	int err = 0;
	int type = 0;
	//printf("curExp: %.*s\n", len, str);
	char* ptr = str;
	while (ptr - str <len) {
#define LEN (len - (ptr-str))
		ptr = getNextNotSpaceChar(ptr, LEN);
		char* nextLeftBracket = NULL;
		uint32_t bracketLen = 0;
		char* nextAnd = NULL;
		char* nextOr = NULL;

		nextLeftBracket = getNextBracketMultipleExpression(ptr, len - (ptr - str), &bracketLen);
		if (!nextLeftBracket) { nextLeftBracket--; }
		
		nextAnd = strchr(ptr, '&'); if (!nextAnd) { nextAnd--; }
		nextOr = strchr(ptr, '|'); if (!nextOr) { nextOr--; }

		if (nextLeftBracket < nextAnd && nextLeftBracket < nextOr && nextLeftBracket - str < len) {
			//.. ( ... )
			err = stackPushOperatior(head, OT_Left_bracket); if (err < 0) { return -1; }
			err = sqlStrExpressionToStack(tableName, nextLeftBracket, bracketLen, head); if (err < 0) { return -1; }
			err = stackPushOperatior(head, OT_Right_bracket); if (err < 0) { return -1; }

			ptr += bracketLen + 2;

			if (ptr[0] == '&') {
				err = stackPushOperatior(head, OT_And); if (err < 0) { return -2; }
			}
			else if (ptr[0] == '|') {
				err = stackPushOperatior(head, OT_Or); if (err < 0) { return -2; }
			}
			else if(ptr - str >= len){
				break;
			}
			else {
				return -1;
			}
			ptr++;
		}
		else if (nextAnd < nextLeftBracket && nextAnd < nextOr && nextAnd - str < len) {
			//... & ...
			//single expression
			err = sqlStrSingleExpressionToStack(tableName, ptr, nextAnd - ptr, head); if (err < 0) { return -2; }
			err = stackPushOperatior(head, OT_And); if (err < 0) { return -2; }
			ptr = nextAnd + 1;
		}
		else if (nextOr < nextLeftBracket && nextOr < nextAnd && nextOr - str < len) {
			//... | ...
			//single expression
			err = sqlStrSingleExpressionToStack(tableName, ptr, nextOr - ptr, head); if (err < 0) { return -2; }
			err = stackPushOperatior(head, OT_Or); if (err < 0) { return -2; }
			ptr = nextOr + 1;
		}
		else {
			//finaly
			//single expression
			if (ptr[0] == '(' && ptr[len - (ptr - str)-1] == ')') {
				memmove(ptr, ptr+1, len - (ptr - str) - 2);

				len -= 2;
			}
			err = sqlStrSingleExpressionToStack(tableName, ptr, len - (ptr - str), head); if (err < 0) { return -2; }
			break;
		}
	}

	return 0;
}

int calculateNumberExpression(stackNode** head) {
	//1+2 
	//no =
	//必须已知为number类型
	if (head == NULL)
		return -1;
	int err = 0;
	//printf("calculate single exp: "); stackShowByCnt(*head, 3);
	if (stackNodeCnt(*head) < 3)
		return 1;
	stackNode* leftNum = (*head);
	stackNode* op = leftNum->next;
	stackNode* rightNum = op->next;
	sqlDataType dataType = 0;
	operatType opType = 0;
	
	if (leftNum->type.valueType == VT_var || rightNum->type.valueType == VT_var)
		return 1;
	if (leftNum->type.valueType != VT_num || op->type.valueType != VT_operator || rightNum->type.valueType != VT_num)
		return -1;
	if (leftNum->type.dataType != rightNum->type.dataType)
		return -1;
	if (leftNum->type.dataType < SQL_DATA_INT8 || leftNum->type.dataType > SQL_DATA_DOUBLE)
		return -1;
	if (rightNum->type.dataType < SQL_DATA_INT8 || rightNum->type.dataType > SQL_DATA_DOUBLE)
		return -1;
	if (op->type.opType < OT_Plus || op->type.opType > OT_Division)
		return -1;

	dataType = leftNum->type.dataType;
	opType = op->type.opType;

	stackNode* next = NULL;
	stackNode* tmp = stackGetNodeByIndex(*head, 2); if (!tmp) { return -1; }
	next = tmp->next;	tmp->next = NULL;

	rightNum = stackPop(head);
	op = stackPop(head);
	leftNum = stackPop(head);

	switch (dataType)
	{
	case SQL_DATA_INT8:
	{
		if (opType == OT_Division && INT8_VALUE(rightNum->value) == 0) {
			ERROR(-2);
		}
		int8_t value = calculateInt(INT8_VALUE(leftNum->value), INT8_VALUE(rightNum->value), opType);
		err = stackPushNumber(head, dataType, &value);
		break;
	}
	case SQL_DATA_INT16:
	{
		if (opType == OT_Division && INT16_VALUE(rightNum->value) == 0) {
			ERROR(-2);
		}
		int16_t value = calculateInt(INT16_VALUE(leftNum->value), INT16_VALUE(rightNum->value), opType);
		err = stackPushNumber(head, dataType, &value);
		break;
	}
	case SQL_DATA_TABLE:
	case SQL_DATA_INT32:
	{
		if (opType == OT_Division && INT32_VALUE(rightNum->value) == 0) {
			ERROR(-2);
		}
		int32_t value = calculateInt(INT32_VALUE(leftNum->value), INT32_VALUE(rightNum->value), opType);
		err = stackPushNumber(head, dataType, &value);
		break;
	}
	case SQL_DATA_INT64:
	{
		if (opType == OT_Division && INT64_VALUE(rightNum->value) == 0) {
			ERROR(-2);
		}
		int64_t value = calculateInt(INT64_VALUE(leftNum->value), INT64_VALUE(rightNum->value), opType);
		err = stackPushNumber(head, dataType, &value);
		break;
	}
	case SQL_DATA_FLOAT:
	{
		if (opType == OT_Division && FLOAT_VALUE(rightNum->value) == 0) {
			ERROR(-2);
		}
		float value = calculateInt(FLOAT_VALUE(leftNum->value), FLOAT_VALUE(rightNum->value), opType);
		err = stackPushNumber(head, dataType, &value);
		break;
	}
	case SQL_DATA_DOUBLE:
	{
		if (opType == OT_Division && DOUBLE_VALUE(rightNum->value) == 0) {
			ERROR(-2);
		}
		double value = calculateInt(DOUBLE_VALUE(leftNum->value), DOUBLE_VALUE(rightNum->value), opType);
		err = stackPushNumber(head, dataType, &value);
		break;
	}
	default:
		ERROR(-2);
	}
	if (err < 0) { ERROR(-2); }


	stackFreeNode(&rightNum);
	stackFreeNode(&op);
	stackFreeNode(&leftNum);

	(*head)->next = next;
	//printf("calculate done: "); stackShowByCnt(*head, 1);
	return 0;

error:
	stackFreeNode(&rightNum);
	stackFreeNode(&op);
	stackFreeNode(&leftNum);

	printf("calculate fail\n");
	return -2;
}
int calculateMultipleExpression(stackNode** head, uint32_t cnt, uint32_t* outCnt) {
	//a+(2+3)+3
	//no =
	if (head == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	if (stackNodeCnt(*head) < cnt)
		return -1;
	if (cnt < 3) {
		if (outCnt) {
			*outCnt = cnt;
		}
		return 0;
	}
	int err = 0;
	stackNode* headTail = stackGetTailNode(*head, cnt);
	stackNode* exp = NULL;
	stackNode* expTail = NULL;
	stackNode* ptr = *head;
	uint8_t hasCalculated = 0;
	uint32_t expCnt = 0;

	for (uint32_t i = 0; i < cnt;i++,ptr = ptr->next) {
		//stackShow(*head);
		//stackShow(ptr);
		//stackShow(exp);
		if (ptr->type.valueType == VT_operator) {
			//计算前面的算式
			expCnt = stackNodeCnt(exp);
			while (expCnt >= 3) {
				stackNode* leftLast = expCnt > 3 ? stackGetNodeByIndex(exp, expCnt - 4) : NULL;
				stackNode* leftNode = !leftLast ? stackGetNodeByIndex(exp, expCnt - 3) : leftLast->next;
				stackNode* opNode = leftNode->next;
				stackNode* rightNode = opNode->next;

				if (leftNode->type.valueType != VT_num && leftNode->type.valueType != VT_var) {
					break;
				}
				if (rightNode->type.valueType != VT_num && rightNode->type.valueType != VT_var) {
					break;
				}

				if (OP_LEVEL(opNode->type.opType) < OP_LEVEL(ptr->type.opType))
					break;

				err = calculateNumberExpression(!leftLast ? &exp : &leftLast->next); if (err < 0) { ERROR(-1) }
				if (err == 1) {
					//var or cnt < 3
					break;
				}
				expCnt = stackNodeCnt(exp);
				hasCalculated = 1;
			}
			
		}
		
		err = stackPush(&exp, ptr->type, ptr->value, ptr->size); if (err < 0) {ERROR(-1)}

		if (i == cnt - 1) {
			//final
			//计算前面的算式
			uint32_t expCnt = stackNodeCnt(exp);
			while (expCnt >= 3) {
				stackNode* leftLast = expCnt > 3 ? stackGetNodeByIndex(exp, expCnt - 4) : NULL;
				stackNode* leftNode = !leftLast ? stackGetNodeByIndex(exp, expCnt - 3) : leftLast->next;
				stackNode* opNode = leftNode->next;
				stackNode* rightNode = opNode->next;

				if (leftNode->type.valueType != VT_num && leftNode->type.valueType != VT_var) {
					break;
				}
				if (rightNode->type.valueType != VT_num && rightNode->type.valueType != VT_var) {
					break;
				}

				if (OP_LEVEL(opNode->type.opType) < ptr->type.opType)
					break;

				err = calculateNumberExpression(!leftLast ? &exp : &leftLast->next); if (err < 0) { ERROR(-1) }
				if (err == 1) {
					//var or cnt < 3
					break;
				}
				expCnt = stackNodeCnt(exp);
				hasCalculated = 1;
			}
		}
	
		//(x)
		if (ptr->type.opType == OT_Right_bracket) {
			expCnt = stackNodeCnt(exp);
			//stackShow(exp);
			if (expCnt >= 3) {
				stackNode* left = stackGetNodeByIndex(exp, expCnt - 3);
				stackNode* mid = left->next;
				stackNode* right = mid->next;

				if (left->type.valueType == VT_operator && left->type.opType == OT_Left_bracket &&
					right->type.valueType == VT_operator && right->type.opType == OT_Right_bracket) {
					err = stackRemoveNodeByPtr(&exp, left); if (err < 0) { ERROR(-1) }
					err = stackRemoveNodeByPtr(&exp, right); if (err < 0) { ERROR(-1) }
				}
			}
		}
	}
	if(!exp)
		ERROR(-1)

	//stackShow(*head);
	if (hasCalculated == 0) {
		if (outCnt) {
			*outCnt = cnt;
		}
		return 0;
	}

	if (outCnt) {
		*outCnt = stackNodeCnt(exp);
	}

	//stackShow(*head);
	//stackShow(exp);
	//stackShow(headTail);

	//copy exp[0] -> head[0]
	expTail = exp; while (expTail->next) { expTail = expTail->next; } //exp tail

	(*head)->type = exp->type;
	free((*head)->value);
	(*head)->value = exp->value;
	(*head)->size = exp->size;

	ptr = (*head)->next;
	expTail->next = headTail->next;
	//stackShow(headTail);
	//stackShow(expTail);
	headTail->next = NULL;
	stackFreeOperatorStack(&ptr);
	(*head)->next = exp->next;
	//stackShow(*head);
	if(exp != expTail)
		free(exp);

	return 0;

error:

	stackFreeOperatorStack(&exp);
	return -2;
}
int calculateContinuousAndExpression(stackNode** head, uint32_t cnt, uint32_t* outCnt) {
	//t&t&(unkown)&t -> t&(unkown)
	//t&f&t -> fasle
	//t&(unkown)&t&t&(unkown)&t&t -> t&(unkown)
	//只计算单层
	if (head == NULL || outCnt == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	if (stackNodeCnt(*head) < cnt)
		return -1;
	stackShowByCnt(*head, cnt); printm("\n");
	int err = 0;
	uint8_t retTrue = 0;
	uint8_t findBool = 0;
	uint8_t findUnknown = 0;
	stackNode* last = NULL;
	stackNode* ptr = *head;

	//is has false
	for (uint32_t i = 0; i < cnt; i++, ptr = ptr->next) {
		if (ptr->type.valueType == VT_Bool && ptr->type.isTrue == 0) {
			//return false
			stackNode* removingTail = stackGetTailNode(*head, cnt);
			stackNode* next = removingTail->next;
			removingTail->next = NULL;
			stackFreeOperatorStack(head);
			err = stackPushBool(head, 1); if (err < 0) { return -1; }
			(*head)->next = next;
			*outCnt = 1;
			return 2;
		}
		if (ptr->type.valueType == VT_Unkown) {
			findUnknown = 1;
		}
	}

	if (findUnknown == 0) {
		//no false and no unkown
		//return true
		stackNode* removingTail = stackGetTailNode(*head, cnt);
		stackNode* next = removingTail->next;
		removingTail->next = NULL;
		stackFreeOperatorStack(head);
		err = stackPushBool(head, 1); if (err < 0) { return -1; }
		(*head)->next = next;
		*outCnt = 1;
		return 1;
	}

	//all is true without unkown
	//简化unkown以外多余的bool, 只保留一个true和所有unkown
	findBool = 0;
	for (uint32_t i = 0; i < cnt && ptr; i++, (*outCnt)++) {
		if (i == 0) { (*outCnt) = 0; }
		if (ptr->type.valueType == VT_operator) {
			ptr = ptr->next;
		}
		else if (ptr->type.valueType == VT_Bool) {
			if (findBool) {
				stackNode* tmp = ptr;
				ptr = ptr->next;
				err = stackRemoveNodeByPtr(head, tmp); if (err < 0) { return -1; }
				i++;
				if (ptr) {
					tmp = ptr;
					ptr = ptr->next;
					err = stackRemoveNodeByPtr(head, tmp); if (err < 0) { return -1; }
					i++;
				}
			}
			else {
				findBool = 1;
			}
		}
		else if (ptr->type.valueType == VT_Unkown) {
			findUnknown = 1;
			ptr = ptr->next;
		}
		else {
			return -1;
		}
	}

	

	return 0;
}
int calculateContinuousOrExpression(stackNode** head, uint32_t cnt, uint32_t* outCnt) {
	//f|t&(unkown)|f -> true
	//f|f|f -> fasle
	//只计算单层
	if (head == NULL || outCnt == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	if (stackNodeCnt(*head) < cnt)
		return -1;
	//stackShowByCnt(*head, cnt);
	int err = 0;
	uint8_t retTrue = 0;
	uint8_t findBool = 0;
	uint8_t findUnknown = 0;
	stackNode* last = NULL;
	stackNode* ptr = *head;

	//is has true
	for (uint32_t i = 0; i < cnt; i++, ptr = ptr->next) {
		if (ptr->type.valueType == VT_Bool && ptr->type.isTrue == 1) {
			//return true
			stackNode* removingTail = stackGetTailNode(*head, cnt);
			stackNode* next = removingTail->next;
			removingTail->next = NULL;
			stackFreeOperatorStack(head);
			err = stackPushBool(head, 1); if (err < 0) { return -1; }
			(*head)->next = next;
			*outCnt = 1;
			return 1;
		}
		if (ptr->type.valueType == VT_Unkown) {
			findUnknown = 1;
		}
	}

	if (findUnknown == 0) {
		//no true and no unkown
		//return false
		stackNode* removingTail = stackGetTailNode(*head, cnt);
		stackNode* next = removingTail->next;
		removingTail->next = NULL;
		stackFreeOperatorStack(head);
		err = stackPushBool(head, 0); if (err < 0) { return -1; }
		(*head)->next = next;
		*outCnt = 1;
		return 2;
	}

	//stackShowByCnt(*head, cnt); printf("\n");
	//all is false without unkown
	//简化unkown以外多余的bool, 只保留一个false和所有unkown
	findBool = 0;
	(*outCnt) = 0;
	ptr = *head;
	for (uint32_t i = 0; i < cnt && ptr; i++) {
		if (ptr->type.valueType == VT_operator) {
			ptr = ptr->next;
			(*outCnt)++;
		}
		else if (ptr->type.valueType == VT_Bool) {
			if (findBool) {
				stackNode* tmp = ptr;
				ptr = ptr->next;
				err = stackRemoveNodeByPtr(head, tmp); if (err < 0) { return -1; }
				i++;
				if (ptr) {
					tmp = ptr;
					ptr = ptr->next;
					err = stackRemoveNodeByPtr(head, tmp); if (err < 0) { return -1; }
					i++;
				}
			}
			else {
				findBool = 1;
				(*outCnt)++;
			}
		}
		else if (ptr->type.valueType == VT_Unkown) {
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
int calculateMultipleBoolExpression(stackNode** head, uint32_t cnt, uint32_t* outCnt) {
	//输入为已经由单表达式计算完毕的，只包含最简unkown exp和bool的表达式集合
	
	if (head == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	if (stackNodeCnt(*head) < cnt)
		return -1;
	//stackShowByCnt(*head, cnt);
	int err = 0;
	stackNode* nextBracket = *head;
	stackNode* nextBracketLast = NULL;
	stackNode* nextAnd = *head;
	stackNode* ptr = NULL;
	uint32_t newTmpCnt = 0;
#define PTR (ptr?ptr:(*head))

	//判断当前层是否恒为True/False
	//and
	while (1) {
#define CNT (ptr?(cnt - nextAndListCnt - nextAndListIndex - 1):cnt)

		stackNode* nextAndList = NULL;
		stackNode* nextAndListLast = NULL;
		uint32_t nextAndListIndex = 0;
		uint32_t nextAndListCnt = 0;
		//stackShow(PTR);
		//stackShowByCnt(PTR, CNT);
		nextAndList = getNextContinuousAndExpression(PTR, CNT, &nextAndListCnt);
		if (nextAndList) {
			err = stackGetNodeIndex(PTR, CNT, nextAndList, &nextAndListIndex); if (err < 0) { return -1; }
			nextAndListLast = stackGetLastNode(PTR, nextAndList); 
			uint32_t srcCnt = nextAndListCnt;

			err = calculateContinuousAndExpression((nextAndListLast == NULL || nextAndListLast == *head) ? head : (&(nextAndListLast->next)), srcCnt, &newTmpCnt); if (err < 0) { return -1; }
			if (err == 1 ||err == 2){
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
		ptr = stackGetNodeByIndex(PTR, nextAndListIndex + nextAndListCnt - 1); if (!ptr) { return -1; }
		//stackShow(PTR);
	}
	
	//Or
	stackNode* nextOr = stackGetNextOperator(*head, cnt, OT_Or); 
	if (nextOr) { 
		err = calculateContinuousOrExpression(head, cnt, &newTmpCnt); if (err < 0) { return -1; }
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

	//当前层无法计算，进一步计算下一层级
	uint8_t isChangedUnkownList = 0;
	stackNode* nextUnkown = NULL;
	uint32_t nextUnkownIndex = 0;
	nextUnkown = getNextUnkownNodeInStack(*head, cnt);
	while (nextUnkown) {
		uint32_t unkownListCnt = nextUnkown->size;
		uint32_t unkownListNewCnt = 0;
		err = calculateMultipleBoolExpression(&(nextUnkown->value), unkownListCnt, &unkownListNewCnt); if (err < 0) { return -1; }
		if (err == 1 || err == 2) {
			uint8_t isTrue = err;
			stackFreeOperatorStack(&(nextUnkown->value));
			nextUnkown->type.valueType = VT_Bool;
			nextUnkown->type.isTrue = isTrue;
			isChangedUnkownList = 1;
		}
		else {
			//unkown			
		}
		
		nextUnkown = getNextUnkownNodeInStack(nextUnkown->next, cnt);
	}

	//需要重新判断当前层级，因为有unkown子层级可能被转化为bool
	if (isChangedUnkownList) {
		//and
		while (1) {
#define CNT (ptr?(cnt - nextAndListCnt - nextAndListIndex - 1):cnt)

			stackNode* nextAndList = NULL;
			stackNode* nextAndListLast = NULL;
			uint32_t nextAndListIndex = 0;
			uint32_t nextAndListCnt = 0;
			//stackShow(PTR);
			//stackShowByCnt(PTR, CNT);
			nextAndList = getNextContinuousAndExpression(PTR, CNT, &nextAndListCnt);
			if (nextAndList) {
				err = stackGetNodeIndex(PTR, CNT, nextAndList, &nextAndListIndex); if (err < 0) { return -1; }
				nextAndListLast = stackGetLastNode(PTR, nextAndList);
				uint32_t srcCnt = nextAndListCnt;

				err = calculateContinuousAndExpression((nextAndListLast == NULL || nextAndListLast == *head) ? head : (&(nextAndListLast->next)), srcCnt, &newTmpCnt); if (err < 0) { return -1; }
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
			ptr = stackGetNodeByIndex(PTR, nextAndListIndex + nextAndListCnt - 1); if (!ptr) { return -1; }
			//stackShow(PTR);
		}

		//or 
		err = calculateContinuousOrExpression(head, cnt, &newTmpCnt); if (err < 0) { return -1; }
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
int singleExpressionToBool(stackNode** head) {
	//1=1 -> true
	if (head == NULL)
		return -1;
	int err = 0;
	int ret = 0;
	if (stackNodeCnt(*head) < 3) {
		return -1;
	}
	//printf("single to bool: "); stackShowByCnt(*head, 3);

	stackNode* next = NULL;
	stackNode* tmp = stackGetNodeByIndex(*head, 2); if (!tmp) { return -1; }
	next = tmp->next;	tmp->next = NULL;

	stackNode* rightNum = stackPop(head);
	stackNode* op = stackPop(head);
	stackNode* leftNum = stackPop(head);
	sqlDataType dataType = leftNum->type.dataType;
	operatType opType = op->type.opType;

	if (opType < OT_Greater_than || opType > OT_Equal) {
		ERROR(-1);
	}

	uint8_t isTrue = 0;
	if (dataType >= SQL_DATA_INT8 && dataType <= SQL_DATA_TABLE) {
		int64_t leftValue_int = 0;
		int64_t rightValue_int = 0;
		switch (dataType)
		{
		case SQL_DATA_INT8:
		{
			leftValue_int = INT8_VALUE(leftNum->value);
			rightValue_int = INT8_VALUE(rightNum->value);
			break;
		}
		case SQL_DATA_INT16:
		{
			leftValue_int = INT16_VALUE(leftNum->value);
			rightValue_int = INT16_VALUE(rightNum->value);
			break;
		}
		case SQL_DATA_TABLE:
		case SQL_DATA_INT32:
		{
			leftValue_int = INT32_VALUE(leftNum->value);
			rightValue_int = INT32_VALUE(rightNum->value);
			break;
		}
		case SQL_DATA_INT64:
		{
			leftValue_int = INT64_VALUE(leftNum->value);
			rightValue_int = INT64_VALUE(rightNum->value);
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
		err = stackPushBool(head, isTrue);
	}
	else if (dataType >= SQL_DATA_FLOAT && dataType <= SQL_DATA_DOUBLE) {
		double leftValue_double = 0;
		double rightValue_double = 0;

		switch (dataType)
		{
		case SQL_DATA_FLOAT:
		{
			leftValue_double = FLOAT_VALUE(leftNum->value);
			rightValue_double = FLOAT_VALUE(rightNum->value);
			break;
		}
		case SQL_DATA_DOUBLE:
		{
			leftValue_double = DOUBLE_VALUE(leftNum->value);
			rightValue_double = DOUBLE_VALUE(rightNum->value);
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
		err = stackPushBool(head, isTrue);
	}
	else if (dataType >= SQL_DATA_STRING) {
		//string 
		err = calculateStringEquation(leftNum->value, leftNum->size, rightNum->value, rightNum->size, opType); if (err < 0) { ERROR(-1); }
		if (err) {
			isTrue = 1;
			ret = 1;
		}
		else {
			isTrue = 0;
			ret = 0;
		}
		err = stackPushBool(head, isTrue);
	}
	else {
		ERROR(-1);
	}
	if (err < 0) { ERROR(-2); }

	stackFreeNode(&rightNum);
	stackFreeNode(&op);
	stackFreeNode(&leftNum);

	(*head)->next = next;
	//printf("calculate done: "); stackShowByCnt(*head, 1);
	return ret;

error:
	stackFreeNode(&rightNum);
	stackFreeNode(&op);
	stackFreeNode(&leftNum);
	printf("calculate fail\n");
	return -2;
}
int singleExpressionSimplify(stackNode** head, uint32_t cnt, uint32_t* newCnt) {
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
	//printf("sub: "); stackShowByCnt(*head, cnt); printf("\n");
	uint32_t equalIndex = 0;
	uint32_t leftCnt = 0;
	uint32_t rightCnt = 0;
	stackNode* equal = getNextEqualNode(*head, cnt, &equalIndex);
	if (!equal) { ERROR(-1); }

	leftCnt = equalIndex;
	rightCnt = cnt - leftCnt - 1;
	if (getNextEqualNode(equal->next, leftCnt, NULL)) {
		// not subexpression
		printf("not subexpression\n");
		return NULL;
	}

	uint32_t varCnt = varNodeCntInSubStack(head, cnt);
	if (!equal) { ERROR(-1) }

	//stackShowByCnt(*head, cnt);
	err = calculateMultipleExpression(head, leftCnt, &leftCnt); if (err < 0) {ERROR(-1) }
	//stackShowByCnt(*head, leftCnt+rightCnt+1);
	err = calculateMultipleExpression(&(equal->next), rightCnt, &rightCnt); if (err < 0) { ERROR(-1) }
	//stackShowByCnt(*head, leftCnt + rightCnt + 1);

	if (varCnt == 0) {
		//(1 + 2) * 6 = 0
		//直接输出Ture or False

		//去括号
		if ((*head)->type.valueType == VT_operator && (*head)->type.opType == OT_Left_bracket) {
			stackNode* tail = *head; while (tail->next) { tail = tail->next; }
			if (tail->type.valueType != VT_operator && tail->type.opType != OT_Right_bracket) {
				ERROR(-3)
			}
			err = stackRemoveNodeByPtr(head, *head); if (err < 0) { ERROR(-2) }
			err = stackRemoveNodeByPtr(head, tail); if (err < 0) { ERROR(-2) }
		}

		stackNode* leftNum = NULL;
		stackNode* rightNum = NULL;
		stackNode* op = NULL;

		leftNum = *head;
		op = leftNum->next;
		rightNum = op->next;

		if (op->type.valueType != VT_operator || leftNum->type.valueType != rightNum->type.valueType)
			ERROR(-2);

		uint8_t resultBool = 0;
		err = singleExpressionToBool(head, 3); if (err < 0) { ERROR(-2); }
		if (err) {
			resultBool = 1;
			ret = 1;
		}
		else {
			resultBool = 0;
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

	//stackShow(*head);
	return ret;

error:
	return err;
}
int multipleExpressionSimplify(stackNode** head, uint32_t cnt, uint32_t* newCnt) {
	// col1 + co2 > 60 & (col3 * (4 - 1) > 8 | (1 + 2) * 6 = 0) -> col1+col2>60&(col3>2.6666|18=0)

	//1. 根据括号进行分层递归拆解，知道遇到当前层级无括号
	//2. 当前无括号的层级中以&和|拆分为多个子部分，每个子部分单独计算结果，如可计算结果或可化简，需替换原有位置表达式为新值或化简后表达式

	if (head == NULL)
		return -1;
	if (*head == NULL)
		return -1;
	int err = 0;
	int ret = 0;
	stackNode* tail = stackGetNodeByIndex(*head, cnt - 1);
	uint32_t tailIndex = 0;
	//printf("multiple head: "); stackShowByCnt(*head, cnt); printf("\n");
	uint32_t curCnt = cnt;
	stackNode* ptr = NULL;
	uint32_t ptrIndex = 0;

	//uint8_t lastBool = 0;//0: none 1:true 2:false
	while (1) {
		//stackShow(ptr == NULL ? *head : ptr->next);
		stackNode* op_and = NULL;
		stackNode* op_or = NULL;
		stackNode* nextBracket = NULL;
		uint32_t opAndIndex = 0;
		uint32_t opOrIndex = 0;
		uint32_t nextBracketCnt = 0;//size
		uint32_t nextBrackeEqualCnt = 0;
		uint32_t leftBracketIndex = 0;
		uint32_t destIndex = UINT32_MAX;

		uint32_t newSubCnt = 0;

		op_and = stackGetNextOperator(ptr == NULL ? *head : ptr->next, cnt - ptrIndex - 2, OT_And);
		op_or = stackGetNextOperator(ptr == NULL ? *head : ptr->next, cnt - ptrIndex - 2, OT_Or);
		
		err = stackGetNodeIndex(*head, cnt, op_and, &opAndIndex); if (err < 0) { opAndIndex = UINT32_MAX; }
		err = stackGetNodeIndex(*head, cnt, op_or, &opOrIndex); if (err < 0) { opOrIndex = UINT32_MAX; }
		nextBracket = getNextBracketMultipleExpressionInStack(ptr == NULL ? *head : ptr->next, cnt - ptrIndex - 1, &nextBracketCnt);
		if (!nextBracket) { leftBracketIndex = UINT32_MAX; }

		if (opAndIndex >= cnt) { opAndIndex = UINT32_MAX; op_and = NULL; }
		if (opOrIndex >= cnt) { opOrIndex = UINT32_MAX; op_or = NULL; }
		if (leftBracketIndex >= cnt) { leftBracketIndex = UINT32_MAX; nextBracket = NULL; }
		if (destIndex > opAndIndex) { destIndex = opAndIndex; }
		if (destIndex > opOrIndex) { destIndex = opOrIndex; }
		if (destIndex > leftBracketIndex) { destIndex = leftBracketIndex; }
		//stackShow(ptr);
		if (op_and == NULL && op_or == NULL && nextBracket == NULL) {
			//最底层的最后一段

			uint32_t srcCnt = ptr ? (cnt - ptrIndex - 1) : (cnt - ptrIndex);
			err = singleExpressionSimplify(ptr == NULL ? head : &(ptr->next), srcCnt, &newSubCnt);	if (err < 0) { ERROR(-2); }
			if (err == 1 || err == 2) {

			}
			else {
				//unkown
				stackNode* unkown = ptr?ptr->next:*head;
				stackNode* unkownTail = stackGetNodeByIndex(unkown, newSubCnt - 1);
				stackNode* next = unkownTail->next;
				unkownTail->next = NULL;
				if(ptr)
					ptr->next = NULL;
				else
					*head = NULL;

				err = stackPushUnkownList(ptr ? &(ptr->next) : head, unkown, newSubCnt); if (err < 0) { return -1; }
				if (ptr)
					ptr->next->next = next;
				else
					(*head)->next = next;
			}
			cnt -= (srcCnt - 1);
			
			break;
		}
		else if (leftBracketIndex < opAndIndex && leftBracketIndex < opOrIndex) {
			//先递归计算括号

			stackNode* bracketLast = stackGetLastNode(*head, nextBracket);//带括号的
			uint32_t newBracketCnt = 0;

			err = multipleExpressionSimplify(&(bracketLast->next), nextBracketCnt, &curCnt); if (err < 0) { ERROR(-2); }
			//curCnt不包含外边括号

			uint8_t isTrue = UINT8_MAX;
			if (err == 1) {
				isTrue = 1;
			}
			else if (err == 2) {
				isTrue = 0;
			}
			if (err == 1 || err == 2) {
				//移除算式为bool并消除括号

				if (ptr) {
					err = stackRemoveNodeByIndex(head, ptrIndex + 1); if (err < 0) { ERROR(-2) }
					err = stackRemoveNodeByIndex(head, ptrIndex + 2); if (err < 0) { ERROR(-2) }
				}
				else {
					err = stackRemoveNodeByIndex(head, 0); if (err < 0) { ERROR(-2) }
					err = stackRemoveNodeByIndex(head, 1); if (err < 0) { ERROR(-2) }
				}
				bracketLast = stackGetNodeByIndex(*head, ptrIndex);

				cnt -= 2;
			}
			else {
				//unkown
				//需保留两侧括号
				stackNode* sub = bracketLast->next;
				stackNode* subTail = stackGetTailNode(sub, curCnt);
				bracketLast->next = NULL;
				err = stackPushUnkownList(&(bracketLast->next), sub, curCnt); if (err < 0) { return -1; }
				bracketLast->next->next = subTail->next;
				subTail->next = NULL;
				
			}

			cnt -= (nextBracketCnt - 1);//括号内长度
			//stackShow(*head);
			//stackShowByCnt(*head, cnt); printf("\n");

			//cur index -> '('
			if (isTrue == 0 || isTrue == 1)
				if (ptr)
					ptrIndex += 2;
				else
					ptrIndex = 1;
			else
				if (ptr)
					ptrIndex += 4;
				else
					ptrIndex = 5;

		}
		else {
			// & < （
			//stackShow(ptr == NULL ? *head : (ptr->next));
			uint32_t srcCnt = ptr == NULL ? destIndex - ptrIndex : destIndex - ptrIndex - 1;
			err = singleExpressionSimplify(ptr == NULL ? head : &(ptr->next), srcCnt, &newSubCnt);	if (err < 0) { ERROR(-2); }
			if (err == 1 || err == 2) {

			}
			else {
				//unkown
				stackNode* unkown = ptr? ptr->next:*head;
				stackNode* unkownTail = stackGetNodeByIndex(unkown, newSubCnt - 1);
				stackNode* next = unkownTail->next;
				unkownTail->next = NULL;
				if (ptr)
					ptr->next = NULL;
				else
					*head = NULL;
			
				err = stackPushUnkownList(ptr?&(ptr->next):head, unkown, newSubCnt); if (err < 0) { return -1; }
				if(ptr)	
					ptr->next->next = next;
				else {
					(*head)->next = next;
				}
			}
			cnt -= (srcCnt - 1);//子表达式长度一定为1，要么是bool要么是不带括号的unkown

			if (opAndIndex == destIndex) {
				err = stackGetNodeIndex(*head, cnt, op_and, &ptrIndex); if (err) { ERROR(-2) }
			}
			else if (opOrIndex == destIndex) {
				err = stackGetNodeIndex(*head, cnt, op_or, &ptrIndex); if (err) { ERROR(-2) }
			}
			else {
				ERROR(-2)
			}
		}

		ptr = stackGetNodeByIndex(*head, ptrIndex); if (!ptr) { break; }
		//stackShow(ptr);
		if (ptrIndex >= cnt) {
			break;
		}

		//stackShow(*head);
	}

	//stackShow(*head);
	err = calculateMultipleBoolExpression(head, cnt, &cnt); if (err < 0) { return -1; }

	if (newCnt)
		*newCnt = cnt;

	return err;
error:

	return err;
}



