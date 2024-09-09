#define _CRT_SECURE_NO_WARNINGS
#include <string.h>
#include "sqlCmdDecode.h"
#include "sqlWhereDecode.h"
#include "myOperation.h"
#include <malloc.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include "MyTypes.h"
#include "sqlTypes.h"

#define ERROR(errCode) {err = errCode;goto error;}

#define ROUND_UP(a,b) (((a+b-1)/b)*b)
#define SQL_STREAM_BLOCK_SIZE (1024U)

//test
SqlInsertColumnInfo gl_colNameList[10] = {
	{.name = "userName", .type = SQL_DATA_STRING, .dataLen = 0 },
	{.name = "id", .type = SQL_DATA_INT32, .dataLen = 0 },
	{.name = "point", .type = SQL_DATA_FLOAT, .dataLen = 0 },
	{.name = "username2", .type = SQL_DATA_STRING, .dataLen = 0 },
	{.name = "point2", .type = SQL_DATA_FLOAT, .dataLen = 0 },
	{.name = "id2", .type = SQL_DATA_INT32, .dataLen = 0 },
	{.name = "col7", .type = SQL_DATA_INT16, .dataLen = 0 },
	{.name = "col8", .type = SQL_DATA_INT16, .dataLen = 0 },
	{.name = "col9", .type = SQL_DATA_INT16, .dataLen = 0 },
	{.name = "col10", .type = SQL_DATA_INT16, .dataLen = 0 }
};

static char* gl_tableInfoList[TABLE_CNT_MAX] = { NULL };

static int setTableInfo(uint64 tableId, char* src, uint64 size);
static TableInfo* getTableInfo(uint64 tableId);
static uint64 getTableIdByName(char* tablename);

typedef enum {
	ERR_NORMAL = INT32_MAX+1,
	ERR_INVALID_INPUT,
	ERR_MEMORY_NOT_ENOUGH,
	ERR_INVALID_SQL,
	ERR_INVALID_TABLE,
	ERR_INVALID_TABLE_NAME,
	ERR_INVALID_INTO,
	ERR_INVALID_VALUE,
	ERR_INVALID_COL_LIST,
	ERR_INVALID_COL_NAME_TOO_LONE,
	ERR_INVALID_FROM,
	ERR_INVALID_SET,
	ERR_INVALID_WHERE,
	ERR_UNSUPPORT_SQL_IN_SHELL,

}errCode;

sqlDataType getColType(char* tableName, char* colName) {
	uint32_t cnt = sizeof(gl_colNameList) / sizeof(SqlInsertColumnInfo);
	uint64 tableId = getTableIdByName(tableName);
	TableInfo* info = getTableInfo(tableId);
	for (uint32_t i = 0; i < info->colNum;i++) {
		if (strncmp(colName, info->colInfo[i].name, strlen(info->colInfo[i].name)) == 0)
			return info->colInfo[i].dataType;
	}
	return SQL_DATA_INVALID;
}
static uint32 getIntCharSize(int64 num) {
	char buff[50] = { 0 };
	return snprintf(buff, sizeof(buff), "%lld", num);
}
static uint32 getDoubleCharSize(double num) {
	char buff[50] = { 0 };
	return snprintf(buff, sizeof(buff), "%g", num);
}
static uint32 getColValueCharLen(char* value, uint32 size, sqlDataType type) {
	int err = 0;
	uint32 len = 0;
	int64 tmpInt = 0;
	double tmpDouble = 0.0;

	if (type == SQL_DATA_STRING) {
		len = strlen(value);
		if (len > size) {
			len = size;
		}
	}
	else if (type >= SQL_DATA_INT8 && type <= SQL_DATA_INT64) {
		switch (type) {
		case SQL_DATA_INT8:
			tmpInt = *((int8*)value);
			break;
		case SQL_DATA_INT16:
			tmpInt = *((int16*)value);
			break;
		case SQL_DATA_INT32:
			tmpInt = *((int32*)value);
			break;
		case SQL_DATA_INT64:
			tmpInt = *((int64*)value);
			break;
		default:
			break;
		}
		len = getIntCharSize(tmpInt);
	}
	else if (type >= SQL_DATA_FLOAT && type <= SQL_DATA_DOUBLE) {
		if (type == SQL_DATA_FLOAT) {
			tmpDouble = *((float*)value);
		}
		else {
			tmpDouble = *((double*)value);
		}
		len = getDoubleCharSize(tmpDouble);
	}
	else {
		return U32_MAX;
	}

	return len;
}

int removeChrs(char* str, uint32_t size, char* destChr, uint32_t destSize, uint32_t* newLen) {
	if (str == NULL || destChr == NULL || size == 0 || destSize == 0) {
		return -1;
	}
	char* ptr = str;
	for (uint32_t i = 0; i < size; i++) {
		char* dest = destChr;
		uint8_t hasRemoved = 0;
		if (ptr[0] == '\'') {
			char* nextSingleQuotationMark = strchr(ptr+1, '\'');
			if (nextSingleQuotationMark && nextSingleQuotationMark - str <= size) {
				//jump 'str'
				uint32 jumpLen = nextSingleQuotationMark - ptr+1;
				ptr += jumpLen;
				i += jumpLen-1;
				continue;
			}
		}
		for (uint32_t j = 0; j < destSize; j++, dest++) {
			if (*ptr == *dest) {
				uint32_t remainLen = size - (ptr - str);
				memmove(ptr, ptr + 1, remainLen - 1);
				hasRemoved = 1;
				break;
			}
		}
		if (!hasRemoved) {
			ptr++;
		}
	}
	if (newLen)
		*newLen = (ptr - str);
	memset(ptr, ' ', size - (ptr - str));
	return 0;
error:

	return -2;
}
int replaceChr(char* str, uint32_t size, char srcChr, char destChr) {
	if (str == NULL)
		return ERR_INVALID_INPUT;
	for (uint32_t i = 0; i < size; i++) {
		if (str[i] == srcChr)
			str[i] = destChr;
	}
	return 0;
}
bool isValidTableName(char* name, uint32 len) {
	bool lenValid = (len < TABLE_NAME_LEN_MAX);
	const char* illegal_chars[] = {
	" ",  // 空格
	"!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "-", "+", "=",
	"{", "}", "[", "]", "|", "\\", ":", ";", "\"", "'", "<", ">",
	",", ".", "?", "/", "~", "`"
	};
	const char invalidChars[] = " !@#$%^&*()-+={}[]|\\:;\"'<>,.?/~`";
	char* ptr = strpbrk(name, invalidChars);
	bool charValid = !ptr ? FALSE : ((ptr - name) >= len);
	bool wordValid = (
		!(strncmp(name, "select", len) == 0) &&
		!(strncmp(name, "update", len) == 0) &&
		!(strncmp(name, "insert", len) == 0) &&
		!(strncmp(name, "create", len) == 0) &&
		!(strncmp(name, "delete", len) == 0) &&
		!(strncmp(name, "from", len) == 0) &&
		!(strncmp(name, "where", len) == 0) &&
		!(strncmp(name, "join", len) == 0) &&
		!(strncmp(name, "on", len) == 0) &&
		!(strncmp(name, "group by", len) == 0) &&
		!(strncmp(name, "order by", len) == 0) &&
		!(strncmp(name, "having", len) == 0) &&
		!(strncmp(name, "ditinct", len) == 0) &&
		!(strncmp(name, "alter", len) == 0) &&
		!(strncmp(name, "drop", len) == 0) &&
		!(strncmp(name, "table", len) == 0) &&
		!(strncmp(name, "view", len) == 0) &&
		!(strncmp(name, "index", len) == 0) &&
		!(strncmp(name, "database", len) == 0) &&
		!(strncmp(name, "user", len) == 0) &&
		!(strncmp(name, "grant", len) == 0) &&
		!(strncmp(name, "show", len) == 0) &&
		!(strncmp(name, "show tables", len) == 0) &&
		!(strncmp(name, "show columns", len) == 0) &&
		!(strncmp(name, "revoke", len) == 0)
		);

	return lenValid && charValid && wordValid;
}

char* getSelectColList(char* sql, uint32_t* len) {
	if (sql == NULL || len == NULL)
		return NULL;
	char* colList = strstr(sql, "select"); if (!colList) { return NULL; }
	colList += 6;
	char* from = strstr(colList, "from"); if (!colList) { return NULL; }
	*len = from - colList;
	int err = removeChrs(colList, *len, " ", 1, len);
	return colList;
}
char* getSelectTable(char* sql, uint32_t* len) {
	if (sql == NULL || len == NULL)
		return NULL;
	char* table = strstr(sql, "from"); if (table == NULL) { return NULL; }
	table += 5;
	char* wheree = strstr(table, "where"); 
	if (wheree)
		*len = wheree - table;
	else
		*len = strlen(sql) - (table - sql);
	int err = removeChrs(table, *len, " ", 1, len);

	return table;
}
char* getSelectWhere(char* sql, uint32_t* len) {
	if (sql == NULL || len == NULL)
		return NULL;
	char* wheree = strstr(sql, "where"); 
	if (wheree) {
		wheree += 6;
		*len = strlen(sql) - (wheree - sql);
	}
	return wheree;
}

char* getInsertTable(char* sql, uint32_t* len) {
	//insert into table1 (col1, col2, ...) value ('str1', 123, ...)	
	if (sql == NULL || len == NULL)
		return NULL;
	char* table = strstr(sql, "into"); if (table == NULL) { return NULL; }
	table += 4;
	char* leftBracket = strchr(table, '('); if (!leftBracket) { return NULL;}
	*len = leftBracket - table;

	int err = removeChrs(table, *len, " ", 1, len);
	return table;
}
int getInsertColValueCnt(char* sql) {
	if (sql == NULL)
		return NULL;
	uint32_t colListLen = 0;
	char* colList = getNexBracket(sql, strlen(sql), &colListLen); if (colList == NULL) { return -1; }
	int cnt = 0;
	
	char* ptr = colList;
	char* comma = strchr(ptr, ',');
	for (;comma && comma - colList < colListLen;cnt++) {
		comma = strchr(ptr, ',');
		ptr = comma + 1;
	}

	return cnt;
}
int getInsertColValueByIndex(char* sql, uint32_t index, char** outColName, uint32_t* outColNameLen, char** outValueStr, uint32_t* outValueStrLen) {
	if (!sql || !outColName || !outColNameLen || !outValueStr || !outValueStrLen) { return -1; }
	int cnt = getInsertColValueCnt(sql); if (cnt <= 0) { return -1; }

	char* colList = NULL;
	char* valueList = NULL;
	uint32_t colListLen = 0;
	uint32_t valueListLen = 0;
	colList = getNexBracket(sql, strlen(sql), &colListLen); if (colList == NULL) { return -1; }
	valueList = getNexBracket(colList + colListLen + 2, strlen(sql) - (colList + colListLen + 2 - sql), &valueListLen); if (valueList == NULL) { return -1; }

	char* colPtr = colList;
	char* valuePtr = valueList;
	for (uint32_t i = 0; i < cnt;i++) {
		colPtr = getNextNotSpaceChar(colPtr, colListLen - (colPtr - colList));
		valuePtr = getNextNotSpaceChar(valuePtr, valueListLen - (valuePtr - colList));

		char* colName = colPtr;
		char* value = valuePtr;
		uint32_t colNameLen = 0;
		uint32_t valueLen = 0;

		char* comma = NULL;
		comma = strchr(colPtr, ','); if (comma && comma - colList >= colListLen) { comma = NULL; }
					
		if (comma) {
			colNameLen = comma - colPtr;
			colPtr = comma + 1;
			comma = strchr(valuePtr, ','); if (comma && comma - valueList >= valueListLen) { comma = NULL; } if (!comma) { return -1; }
			valueLen = comma - valuePtr;
			valuePtr = comma + 1;
		}
		else {
			colNameLen = colListLen - (colPtr - colList);
			valueLen = valueListLen - (valuePtr - valueList);
		}
		
		if (i == index) {
			*outColName = colName;
			*outColNameLen = colNameLen;
			*outValueStr = value;
			*outValueStrLen = valueLen;
			return 0;
		}
	}

	return -1;
}

char* getUpdateTable(char* sql, uint32_t* len) {
	//update table1 set col1 = col2+col3, col2 = 25 where condition
	if (sql == NULL || len == NULL)
		return NULL;
	char* table = strstr(sql, "update"); if (table == NULL) { return NULL; }
	table += 6;
	char* set = strstr(table, "set"); if (set == NULL) { return NULL; }
	*len = set - table;
	int err = removeChrs(table, *len, " ", 1, len); if (err < 0) { return NULL; }
	return table;
}
char* getUpdateSetList(char* sql, uint32_t* len, uint32_t* listCnt) {
	if (sql == NULL || len == NULL || listCnt == NULL)
		return NULL;
	char* set = strstr(sql, "set"); if (set == NULL) { return NULL; }
	set += 3;
	char* wheree = strstr(set, "where"); 
	if (wheree) {
		*len = wheree - set;
	}
	else {
		*len = strlen(sql) - (set - sql);
	}

	char* comma = NULL;
	char* ptr = set;
	for (*listCnt = 1;; (*listCnt)++, ptr = comma + 1) {
		comma = strchr(ptr, ',');
		if (!comma)
			break;
	}

	return set;
}
char* getUpdateSetByIndexFromSetList(char* setList, uint32_t len, uint32_t index, uint32_t* setLen) {
	if (setList == NULL || setLen == NULL) {
		return NULL;
	}
	char* set = setList;
	char* comma = strchr(set, ',');
	for (uint32_t i = 1; i <= index && comma && set - setList < len;i++) {
		set = comma + 1;
		comma = strchr(set, ','); if (comma - setList > len) { comma = NULL; }
	}	
	if (comma) {
		*setLen = comma - set;
	}
	else {
		*setLen = len - (set - setList);
	}
	return set;
}
char* getUpdateWhere(char* sql, uint32_t* len) {
	if (sql == NULL || len == NULL)
		return NULL;
	char* wheree = strstr(sql, "where"); if (wheree == NULL) { return NULL; };
	wheree += 5;
	*len = strlen(sql) - (wheree - sql);
	return wheree;
}

char* getCreateTable(char* sql, uint32_t* len) {
	//create table table1 ( col1 int8 index, col2 string(20), col3 float )
	if (sql == NULL || len == NULL)
		return NULL;
	char* table = strstr(sql, "table"); if (table == NULL) { return NULL; }
	table += 5;
	char* leftBracket = strchr(table, '(');  if (leftBracket == NULL) { return NULL; }
	*len = leftBracket - table;
	int err = removeChrs(table, *len, " ", 1, len);
	return table;
}
char* getCreateColInfoList(char* sql, uint32_t *outlen, uint32_t* colCnt) {
	//create table table1 ( col1 int8 index, col2 string(20), col3 float )
	if (sql == NULL || outlen == NULL || colCnt == NULL)
		return NULL;
	uint32_t colInfoListLen = 0;
	char* colInfo = getNexBracket(sql, strlen(sql), &colInfoListLen);
	char* comma = colInfo;
	for (*colCnt = 0; comma && comma - colInfo < colInfoListLen; (*colCnt)++) {
		comma = strchr(comma, ',');
		if (comma)
			comma++;
	}
	*outlen = colInfoListLen;
	//int err = removeChrs(colInfo, *outlen, " ", 1, outlen); if (err < 0) { return NULL; }

	return colInfo;
}
char* getCreateColNameByIndexFromColInfoList(char* list, uint32_t len, uint32_t index,uint32_t* outLen) {
	if (list == NULL || outLen == NULL)
		return NULL;
	char* info = getNextNotSpaceChar(list, len); if (info == NULL) { return NULL; }
	uint32_t subLen = getNextSubStrLenBeforeComma(info, len - (info - list));//逗号间隔的长度
	for (uint32_t i = 0; i < index && info && info-list < len; i++) {
		subLen = getNextSubStrLenBeforeComma(info, len - (info - list));
		if (subLen == len - (info - list)) { return NULL; }
		info += subLen + 2;
	}
	*outLen = getNextSubStrLenBeforeSpace(info, subLen);
	//int err = removeChrs(info, *outLen, " ", 1, outLen); if (err < 0) { return NULL; }
	return info;
}
char* getCreateDataTypeByIndexFromColInfoList(char* list, uint32_t len, uint32_t index, uint32_t* outLen) {
	if (list == NULL || outLen == NULL)
		return NULL;
	char* info = getNextNotSpaceChar(list, len); if (info == NULL) { return NULL; }
	uint32_t subLen = getNextSubStrLenBeforeComma(info, len - (info - list));//逗号间隔的长度
	for (uint32_t i = 0; i < index && info && info - list < len; i++) {
		subLen = getNextSubStrLenBeforeComma(info, len - (info - list));
		if (subLen == len - (info - list)) { return NULL; }
		info += subLen + 2;
	}
	subLen = getNextSubStrLenBeforeComma(info, len - (info - list)); 
	//info -> colname type
	char* colName = info;
	char* dataType = colName;
	uint32_t subSubLen = 0;//空格间长度
	subSubLen = getNextSubStrLenBeforeSpace(dataType, subLen);
	dataType += subSubLen+1;
	dataType = getNextNotSpaceChar(dataType, subLen - (dataType-colName));
	subSubLen = getNextSubStrLenBeforeSpace(dataType, subLen - (dataType - colName));
	*outLen = subSubLen;
	return dataType;
}
char* getCreateAttributeByIndexFromColInfoList(char* list, uint32_t len, uint32_t index, uint32_t* outLen) {
	if (list == NULL || outLen == NULL)
		return NULL;
	uint32_t dataTypeLen = 0;
	char* dataType = getCreateDataTypeByIndexFromColInfoList(list, len, index, &dataTypeLen); if (dataType == NULL) { return NULL; }
	char* attr = getNextNotSpaceChar(dataType + dataTypeLen, len - (dataType + dataTypeLen - list));
	uint32_t attrLen = getNextSubStrLenBeforeComma(attr, len - (attr - list));
	*outLen = attrLen;
	return attr;
}

char* getDeleteTable(char* sql, uint32_t* len) {
	//delete from table1 where condition
	if (sql == NULL || len == NULL)
		return NULL;
	char* table = strstr(sql, "from"); if (table == NULL) { return NULL; }
	table += 4;
	char* wheree = strstr(table, "where");  if (wheree == NULL) { return NULL; }
	*len = wheree - table;
	int err = removeChrs(table, *len, " ", 1, len);
	return table;
}
char* getDeleteWhere(char* sql, uint32_t* len) {
	if (sql == NULL || len == NULL)
		return NULL;
	char* wheree = strstr(sql, "where");  if (wheree == NULL) { return NULL; }
	wheree += 5;
	*len = strlen(wheree);
	int err = removeChrs(wheree, *len, " ", 1, len);
	return wheree;
}

char* getShowTablesList(char* sql, uint32_t* len) {
	int err = 0;
	char* tag = "show tables";
	char* tableList = strstr(sql, tag) + strlen(tag) + 1; if (!tableList) { return NULL; }
	*len = strlen(tableList);
	tableList = getNextNotSpaceChar(tableList, *len);
	if (tableList) {
		* len = strlen(tableList);
		err = removeChrs(tableList, *len, " ", 1, len); if (err) { *len = 0; return NULL; }
		
	}
	else { *len = 0; }
	return tableList;
}
char* getShowColumnsFrom(char* sql, uint32_t* len) {
	int err = 0;
	char* tag = "from";
	char* from = strstr(sql, tag) + strlen(tag) + 1; if (!from) { return NULL; }
	*len = strlen(from);
	from = getNextNotSpaceChar(from, *len);
	if (from) {
		*len = strlen(from);
		err = removeChrs(from, *len, " ", 1, len); if (err) { *len = 0; return NULL; }
	}
	else { *len = 0; }
	
	return from;
}
char* getShowColumnsList(char* sql, uint32_t* len) {
	int err = 0;
	char* tag = "show columns";
	char* colList = strstr(sql, tag) + strlen(tag) + 1; if (!colList) { return NULL; }
	char* from = strstr(sql, "from");
	if (!from) {
		*len = 0;
		return NULL;
	}
	*len = from - colList;
	if (*len == 0) {
		colList = NULL;
	}
	
	err = removeChrs(colList, *len, " ", 1, len); if (err) { *len = 0; return NULL; }
	return colList;
}

int setCreateColInfoAttributes(char* colInfoAttribute, uint32_t len, SqlColumnItem* item) {
	if (!colInfoAttribute) { return -1; }
	char* attr = strstr(colInfoAttribute, "index");
	if (attr && attr - colInfoAttribute < len) {
		item->index = 1;
	}
	 attr = strstr(colInfoAttribute, "super");
	if (attr && attr - colInfoAttribute < len) {
		item->super = 1;
	}
	 attr = strstr(colInfoAttribute, "unique");
	if (attr && attr - colInfoAttribute < len) {
		item->unique = 1;
	}
	 attr = strstr(colInfoAttribute, "not null");
	if (attr && attr - colInfoAttribute < len) {
		item->notNull = 1;
	}
	 attr = strstr(colInfoAttribute, "single");
	if (attr && attr - colInfoAttribute < len) {
		item->single = 1;
	}

	return 0;
}
sqlDataType str2DataType(char* str, uint32_t len, uint32_t* stringLen) {
	if (str == NULL)
		return SQL_DATA_INVALID;
	int err = 0;
	char* type = NULL;
	type = strstr(str, "int8");
	if (type && type - str < len) {
		return SQL_DATA_INT8;
	}
	type = strstr(str, "int16");
	if (type && type - str < len) {
		return SQL_DATA_INT16;
	}
	type = strstr(str, "int32");
	if (type && type - str < len) {
		return SQL_DATA_INT32;
	}
	type = strstr(str, "int64");
	if (type && type - str < len) {
		return SQL_DATA_INT64;
	}
	type = strstr(str, "float");
	if (type && type - str < len) {
		return SQL_DATA_FLOAT;
	}
	type = strstr(str, "double");
	if (type && type - str < len) {
		return SQL_DATA_DOUBLE;
	}
	type = strstr(str, "string");
	if (type && type - str < len) {
		if (!stringLen)
			return SQL_DATA_INVALID;
		char* leftBracket = strchr(type, '(');
		char* rightBracket = strchr(type, ')');
		if(!leftBracket || !rightBracket || leftBracket-str > len || rightBracket - str > len || leftBracket >= rightBracket)
			return SQL_DATA_INVALID;
		int64_t num = 0;
		err = str2Int64(leftBracket + 1, rightBracket - leftBracket - 1, NULL, &num); if (err < 0 || num <= 0 || num >= UINT32_MAX) { return SQL_DATA_INVALID; }
		*stringLen = num;
		return SQL_DATA_STRING;
	}
	type = strstr(str, "image");
	if (type && type - str < len) {
		return SQL_DATA_IMAGE;
	}
	type = strstr(str, "table");
	if (type && type - str < len) {
		return SQL_DATA_TABLE;
	}
	type = strstr(str, "binary");
	if (type && type - str < len) {
		return SQL_DATA_BINARY;
	}
	
	return SQL_DATA_INVALID;
}
int dataType2Str(sqlDataType type, char* outStr, uint32_t maxLen, uint32_t stringLen) {
	if (outStr == NULL)
		return -1;
	switch (type)
	{
	case SQL_DATA_STRING:
		sprintf(outStr, "string(%u)", stringLen);
		break;
	case SQL_DATA_INT8:
		sprintf(outStr, "int8");
		break;
	case SQL_DATA_INT16:
		sprintf(outStr,"int16");
		break;
	case SQL_DATA_INT32:
		sprintf(outStr,"int32");
		break;
	case SQL_DATA_INT64:
		sprintf(outStr,"int64");
		break;
	case SQL_DATA_FLOAT:
		sprintf(outStr,"float");
		break;
	case SQL_DATA_DOUBLE:
		sprintf(outStr,"double");
		break;
	case SQL_DATA_TABLE:
		sprintf(outStr,"table");
		break;
	case SQL_DATA_IMAGE:
		sprintf(outStr,"image");
		break;
	case SQL_DATA_BINARY:
		sprintf(outStr,"binary");
		break;
	default:
		sprintf(outStr,"invalid");
		break;
	}
	return 0;
}
static int colInfoItem2Str(SqlColumnItem* info) {
	if (info == NULL)
		return -1;
	int err = 0;
	char tmp[50] = { 0 };
	printf("%s ", info->name);
	err = dataType2Str(info->dataType, tmp, sizeof(tmp), info->strMaxLen); if (err < 0) { return -1; }
	printf("%s", tmp);//dataType
	if (info->index || info->super|| info->single|| info->unique|| info->notNull) {
		if (info->index) {
			printf(" index");
		}
		if (info->super) {
			printf(" super");
		}
		if (info->single) {
			printf(" single");
		}
		if (info->unique) {
			printf(" unique");
		}
		if (info->notNull) {
			printf(" not null");
		}
	}

}
static int getColSize(SqlColumnItem* info) {
	switch (info->dataType) {
	case SQL_DATA_STRING:
	case SQL_DATA_BINARY:
		return info->strMaxLen;
	case SQL_DATA_INT8:
		return 1;
	case SQL_DATA_INT16:
		return 2;
	case SQL_DATA_INT32:
		return 4;
	case SQL_DATA_INT64:
		return 8;
	case SQL_DATA_FLOAT:
		return 4;
	case SQL_DATA_DOUBLE:
		return 8;	
	default:
		return -1;
	}

	return -1;
}

static int setTableInfo(uint64 tableId, char* src, uint64 size) {
	if (gl_tableInfoList[tableId]) {
		free(gl_tableInfoList[tableId]);
	}
	gl_tableInfoList[tableId] = malloc(size);
	if (!gl_tableInfoList[tableId]) { return -2; }
	memcpy(gl_tableInfoList[tableId], src, size);
	return 0;
}
static TableInfo* getTableInfo(uint64 tableId) {
	TableInfo* info = gl_tableInfoList[tableId];
	return info;
}
static uint64 getTableIdByName(char* tablename) {
	uint32 cnt = sizeof(gl_tableInfoList);
	for (uint32 i = 0; i < cnt;i++) {
		TableInfo* info = getTableInfo(i);
		if (strncmp(tablename, info->name, strlen(info->name)) == 0) {
			return i;
		}
	}
	return U64_MAX;
}

int SqlDecodeSetTableInfo(uint64 tableId, char* src, uint64 size) {
	return setTableInfo(tableId, src, size);
}

int sqlCmdGetColSize(SqlColumnItem* info) {
	return getColSize(info);
}
void sqlCmdViewColumnInfo(SqlColumnItem* info) {
	int err = colInfoItem2Str(info);
	return;
}
int sqlCmd(char* sql, uint32_t sqlLen, char** stream, uint32_t* streamSize, uint32_t dataCnt, ...) {	
	int err = 0;
	
	return 0;
error:
	switch (err)
	{
	case -1:
		printf("Invalid input\n");
		break;
	case -2:
		printf("Invalid cmd, should input [select | update | insert | create | delete]\n");
		break;
	case -3:
		printf("where decode fail\n");
		break;
	case -4:
		printf("Reset size fail\n");
		break;
	case -5:
		printf("String len too long\n");
		break;
	case -9:
		printf("buffer not enough\n");
	default:
		printf("unknown error\n");
		break;
	}
	{
		
	}
	//va_end(args);
	if(err != -1)	
		free(*stream);
	return err;
}
int sqlCmdByShell(char* sql, uint32_t sqlLen, char** stream, uint32_t* streamSize) {
	// select col1,col2 from table1 where 3*2=6 & 1+2=3 & col3=8 & ((6*3)+col3*(4-1)>8-3+(6*2)/2) | (2*6<8 | 30>0) & (8-2<0)
	// create table table1 ( col1 int8 index, col2 string(20), col3 float )
	// insert into table1 (col1, col2, ...) value ('str1', 123, ...)
	// update table1 set col1 = col2+col3, col2 = 25 where condition
	// delete from table1 where condition
	// drop table table1
	
#define PTR_LEN (sqlLen-(ptr-sql))
	errCode err = 0;
	char* buff = NULL;
	char* setBuff = NULL;
	char* whereBuff = NULL;
	uint32_t whereBuffSize = 0;
	stackNode* exp = NULL;
	if (sql == NULL || stream == NULL || streamSize == NULL)
		ERROR(ERR_INVALID_INPUT);
	if (*stream != NULL)
		ERROR(ERR_INVALID_INPUT);

	err = removeChrs(sql, sqlLen, "\n", 1, &sqlLen); if (err < 0) {ERROR(ERR_INVALID_INPUT) }

	char* ptr = sql;
	//SqlCmdTag tag = { 0 };
	uint32_t memSize = 0;
	if (strncmp(sql, "select", 6) == 0) {
		char* colList = NULL;
		char* table = NULL;
		char* wheree = NULL;
		uint32_t colListLen = 0;
		uint32_t tableLen = 0;
		uint32_t whereLen = 0;

		table = getSelectTable(sql, &tableLen);
		colList = getSelectColList(sql, &colListLen);
		wheree = getSelectWhere(sql, &whereLen);

		if (wheree) {
			//if (colListLen == 1 && colList[0] == '*') {ERROR(ERR_INVALID_WHERE) }
			err = removeChrs(wheree, whereLen, " ", 1, &whereLen); if (err < 0) { ERROR(ERR_INVALID_WHERE) }
			err = expressionWhereCmd(table, wheree, whereLen, &whereBuff, &whereBuffSize); if (err < 0) {ERROR(ERR_INVALID_WHERE) }

		}

		memSize = 
			(wheree ? sizeof(SqlCmdTag) * 3 : sizeof(SqlCmdTag) * 2) +
			(colListLen) +
			(tableLen) +
			(whereBuff ? whereBuffSize : 0);

		*stream = malloc(memSize); if (*stream == NULL) {ERROR(ERR_MEMORY_NOT_ENOUGH) }
		memset(*stream, 0, memSize);
		ptr = *stream;

		SqlCmdTag* tag = *stream;
		tag->cmd = SQL_CMD_SELECT;
		tag->size = colListLen;
		ptr = tag + 1;
		memcpy(ptr, colList, colListLen);

		tag = ptr + tag->size;
		tag->cmd = SQL_CMD_FROM;
		tag->size = tableLen;
		ptr = tag + 1;
		memcpy(ptr, table, tableLen);

		if (wheree) {
			tag = ptr + tag->size;
			tag->cmd = SQL_CMD_WHERE;
			tag->size = whereBuffSize;
			memcpy(tag+1, whereBuff, whereBuffSize);
		}
		*streamSize = memSize;
	}
	else if (strncmp(sql, "update", 6) == 0) {
		//update table1 set col1 = col2 + col3, col2 = 25 where condition
		//tag(update) + tableName + tag(set) + stack(col1 = col2 + col3)... + tag(where) + whereExpressionStack
		char* table = NULL;
		char* setList = NULL;
		char* wheree = NULL;
		uint32_t tableLen = 0;
		uint32_t setListLen = 0;
		uint32_t setCnt = 0;
		uint32_t whereLen = 0;

		table = getUpdateTable(sql, &tableLen);
		setList = getUpdateSetList(sql, &setListLen, &setCnt);
		wheree = getUpdateWhere(sql, &whereLen);

		//buffer out
		uint32_t buffMaxSize = 4096;
		buff = malloc(buffMaxSize); if (!buff) { ERROR(ERR_MEMORY_NOT_ENOUGH) } memset(buff, 0, buffMaxSize);
		char* ptrBuff = buff;

		//update
		SqlCmdTag* tagUpdate = ptrBuff;		ptrBuff += sizeof(SqlCmdTag);
		tagUpdate->cmd = SQL_CMD_UPDATE;	tagUpdate->size = tableLen;
		
		//table
		memcpy(ptrBuff, table, tableLen);	ptrBuff += tagUpdate->size;

		//setList
		for (uint32_t i = 0; i < setCnt; i++) {
			//set tag
			SqlCmdTag* tagSet = ptrBuff;	ptrBuff += sizeof(SqlCmdTag);
			tagSet->cmd = SQL_CMD_SET;		

			//set exp
			uint32_t setLen = 0;
			char* set = getUpdateSetByIndexFromSetList(setList, setListLen, i, &setLen);

			char* setBuff = NULL;
			uint32_t setBuffLen = 0;
			stackNode* head = NULL;
			err = sqlStrExpressionToStack(table, set, setLen, &head); if (err < 0) { ERROR(ERR_INVALID_SET); }
			if (head->next->type.valueType != VT_operator || head->next->type.opType != OT_Equal) {
				//非赋值操作
				printf("ERR 非赋值操作\n");
				ERROR(ERR_INVALID_SET);
			}
			err = stack2Buffer(head, &setBuff, &setBuffLen, NULL); if (err < 0) { ERROR(ERR_INVALID_SET); }
			memcpy(ptrBuff, setBuff, setBuffLen); ptrBuff += setBuffLen;
			tagSet->size = setBuffLen;
		}


		//where
		if (wheree) {
			//where tag
			SqlCmdTag* tagWhere = ptrBuff;	ptrBuff += sizeof(SqlCmdTag);
			tagWhere->cmd = SQL_CMD_WHERE;	

			char* whereBuff = NULL;
			uint32_t whereBuffLen = 0;
			stackNode* head = NULL;
			err = sqlStrExpressionToStack(table, wheree, whereLen, &head); if (err < 0) { ERROR(ERR_INVALID_WHERE); }
			err = stack2Buffer(head, &whereBuff, &whereBuffLen, NULL); if (err < 0) { ERROR(ERR_INVALID_WHERE); }
			memcpy(ptrBuff, whereBuff, whereBuffLen); ptrBuff += whereBuffLen;
			tagWhere->size = whereBuffLen;
		}

		*stream = buff;
		*streamSize = ptrBuff - buff;
	}
	else if (strncmp(sql, "insert", 6) == 0) {
		//tag + tableName + [SqlInsertColumnInfo,colData]...
		char* table = NULL;
		char* colList = NULL;
		char* valueList = NULL;
		uint32_t tableLen = 0;
		uint32_t colListLen = 0;
		uint32_t valueListLen = 0;
		int colCnt = 0;
		
		table = getInsertTable(sql, &tableLen);
		colCnt = getInsertColValueCnt(sql); if (colCnt <= 0) { ERROR(ERR_INVALID_VALUE) }

		//buffer out
		uint32_t buffMaxSize = 4096;
		buff = malloc(buffMaxSize); if (!buff) { ERROR(ERR_MEMORY_NOT_ENOUGH) } memset(buff, 0, buffMaxSize);
		char* ptrBuff = buff;

		SqlCmdTag* tag = buff;

		tag->cmd = SQL_CMD_INSERT;
		tag->size = tableLen;
		char* tableName = tag + 1;
		memcpy(tableName, table, tableLen);

		tag = tableName + tag->size;
		tag->cmd = SQL_CMD_VALUE;

		{
			//col and value
			ptrBuff = tag + 1;
			for (uint32_t i = 0; i < colCnt; i++) {
				char* colName = NULL;
				char* value = NULL;
				uint32_t colNameLen = 0;
				uint32_t valueLen = 0;
				SqlInsertColumnInfo* info = ptrBuff; ptrBuff += sizeof(SqlInsertColumnInfo);

				err = getInsertColValueByIndex(sql, i, &colName, &colNameLen, &value, &valueLen); if (err < 0) { ERROR(ERR_INVALID_VALUE) }
				sqlDataType type = getColType(table, colName); if (type == SQL_DATA_INVALID) { ERROR(ERR_INVALID_VALUE) }
				info->type = type;
				memcpy(info->name, colName, colNameLen);

				int64_t tmpNumi = 0;
				double tmpNumf = 0;

				//get real value
				if (type >= SQL_DATA_INT8 && type <= SQL_DATA_TABLE) {
					err = removeChrs(value, valueLen, " ", 1, &valueLen); if (err < 0) { ERROR(ERR_INVALID_VALUE) }
					err = str2Int64(value, valueLen, NULL, &tmpNumi); if (err < 0) { ERROR(ERR_INVALID_VALUE) }
				}
				else if (type >= SQL_DATA_FLOAT && type <= SQL_DATA_DOUBLE) {
					err = removeChrs(value, valueLen, " ", 1, &valueLen); if (err < 0) { ERROR(ERR_INVALID_VALUE) }
					err = str2Double(value, valueLen, NULL, &tmpNumf); if (err < 0) { ERROR(ERR_INVALID_VALUE) }
				}
				else if (type == SQL_DATA_STRING) {
					value = getNextSingleQuotes(value, valueLen, &valueLen); if (value == NULL) { ERROR(ERR_INVALID_VALUE) }
				}
				else if (info->type >= SQL_DATA_IMAGE && info->type <= SQL_DATA_BINARY) {
					//可以从base64字符串转化为二进制
					//暂时不做
					ERROR(ERR_UNSUPPORT_SQL_IN_SHELL)
				}
				else {
					ERROR(ERR_INVALID_VALUE);
				}

				switch (type) {
				case SQL_DATA_INT8: {
					int8_t realValue = tmpNumi;
					*((int8_t*)(ptrBuff)) = realValue;
					info->dataLen = sizeof(realValue);
					ptrBuff += sizeof(realValue);
					break;
				}
				case SQL_DATA_INT16: {
					int16_t realValue = tmpNumi;
					*((int16_t*)(ptrBuff)) = realValue;
					info->dataLen = sizeof(realValue);
					ptrBuff += sizeof(realValue);
					break;
				}
				case SQL_DATA_TABLE:
				case SQL_DATA_INT32: {
					int32_t realValue = tmpNumi;
					*((int32_t*)(ptrBuff)) = realValue;
					info->dataLen = sizeof(realValue);
					ptrBuff += sizeof(realValue);
					break;
				}
				case SQL_DATA_INT64: {
					int64_t realValue = tmpNumi;
					*((int64_t*)(ptrBuff)) = realValue;
					info->dataLen = sizeof(realValue);
					ptrBuff += sizeof(realValue);
					break;
				}
				case SQL_DATA_FLOAT: {
					float realValue = tmpNumf;
					*((float*)(ptrBuff)) = realValue;
					info->dataLen = sizeof(realValue);
					ptrBuff += sizeof(realValue);
					break;
				}
				case SQL_DATA_DOUBLE: {
					double realValue = tmpNumf;
					*((double*)(ptrBuff)) = realValue;
					info->dataLen = sizeof(realValue);
					ptrBuff += sizeof(realValue);
					break;
				}
				case SQL_DATA_STRING:
				case SQL_DATA_IMAGE:
				case SQL_DATA_BINARY:
					memcpy(ptrBuff, value, valueLen);
					info->dataLen = valueLen;
					ptrBuff += valueLen;
					break;
				default:
					break;
				}

				tag->size += sizeof(SqlInsertColumnInfo) + info->dataLen;
			}
		}

		*stream = buff;
		*streamSize = ptrBuff - buff;
	}
	else if (strncmp(sql, "delete", 6) == 0) {
		uint32_t tableLen = 0;
		uint32_t whereLen = 0;
		char* table = getDeleteTable(sql, &tableLen);
		char* wheree = getDeleteWhere(sql, &whereLen);

		if (wheree) {
			err = expressionWhereCmd(table, wheree, whereLen, &whereBuff, &whereBuffSize); if (err < 0) { ERROR(ERR_INVALID_WHERE) }
		}
		

		//buffer out
		uint32_t buffMaxSize = 4096;
		buff = malloc(buffMaxSize); if (!buff) { ERROR(ERR_MEMORY_NOT_ENOUGH) } memset(buff, 0, buffMaxSize);
		char* ptrBuff = buff;

		//table
		SqlCmdTag* tagTable = ptrBuff;	ptrBuff += sizeof(SqlCmdTag);
		tagTable->cmd = SQL_CMD_DELETE;
		tagTable->size = tableLen;
		memcpy(ptrBuff, table, tableLen); ptrBuff += tagTable->size;

		//where
		if (wheree) {
			SqlCmdTag* tagWhere = ptrBuff;	ptrBuff += sizeof(SqlCmdTag);
			tagWhere->cmd = SQL_CMD_WHERE;
			tagWhere->size = whereBuffSize;
			
			memcpy(ptrBuff, whereBuff, whereBuffSize);
		}
		*stream = buff;
		*streamSize = ptrBuff - buff;
	}
	else if (strncmp(sql, "create", 6) == 0) {
		//create table table1 ( col1 int8 index, col2 string(20), col3 float )
		char* table = NULL;//string
		char* colInfoList = NULL;//[colname type attribute...],[],[]
		uint32_t tableLen = 0;
		uint32_t colInfoListLen = 0;
		uint32_t colInfoCnt = 0;

		table = getCreateTable(sql, &tableLen); if (table == NULL) { ERROR(ERR_INVALID_INPUT); }
		if (!isValidTableName(table, tableLen)) {
			ERROR(ERR_INVALID_TABLE_NAME);
		}
		colInfoList = getCreateColInfoList(sql, &colInfoListLen, &colInfoCnt); if (colInfoList == NULL) { ERROR(ERR_INVALID_COL_LIST); }
		
		//buffer out
		uint32_t buffMaxSize = 4096;
		buff = malloc(buffMaxSize); if (!buff) { ERROR(ERR_MEMORY_NOT_ENOUGH) } memset(buff, 0, buffMaxSize);
		char* ptrBuff = buff;

		//table
		SqlCmdTag* tagg = ptrBuff;	ptrBuff += sizeof(SqlCmdTag); 
		tagg->cmd = SQL_CMD_CREATE; tagg->size = tableLen + 1;
		memcpy(ptrBuff, table, tableLen);	ptrBuff += tagg->size;
		
		//table
		SqlCmdTag* tagColInfo = ptrBuff;	ptrBuff += sizeof(SqlCmdTag);
		tagColInfo->cmd = SQL_CMD_COL_INFO;	tagColInfo->size = colInfoCnt * sizeof(SqlColumnItem);

		//colInfoList
		for (uint32_t i = 0; i < colInfoCnt;i++) {
			SqlColumnItem* item = ptrBuff; ptrBuff += sizeof(SqlColumnItem);
			uint32_t colNameLen = 0;
			char* colName = getCreateColNameByIndexFromColInfoList(colInfoList, colInfoListLen, i, &colNameLen);
			uint32_t dataTypeLen = 0;
			char* dataType = getCreateDataTypeByIndexFromColInfoList(colInfoList, colInfoListLen, i, &dataTypeLen);
			uint32_t stringLen = 0;
			memcpy(item->name, colName, colNameLen);
			item->dataType = str2DataType(dataType, dataTypeLen, &stringLen);
			if (item->dataType == SQL_DATA_INVALID) {
				ERROR(ERR_INVALID_INPUT);
			}
			if (item->dataType == SQL_DATA_STRING) {
				item->strMaxLen = stringLen;
			}
			else {
				item->strMaxLen = getColSize(item);
			}
			uint32_t attrLen = 0;
			char* attr = getCreateAttributeByIndexFromColInfoList(colInfoList, colInfoListLen, i, &attrLen); 
			if (attr) {
				err = setCreateColInfoAttributes(attr, attrLen, item); if (err < 0) { ERROR(ERR_INVALID_COL_LIST) }
			}
		}

		*stream = buff;
		*streamSize = ptrBuff - buff;
	}
	else if (strncmp(sql, "drop", 4) == 0) {
		//drop table table1
		char* table = strstr(sql, "table"); if (table == NULL) { ERROR(ERR_INVALID_INPUT); }
		table += 5;
		uint32_t tableLen = sqlLen - (table - sql);
		err = removeChrs(table, tableLen, " ", 1, &tableLen); if (err < 0) { ERROR(ERR_INVALID_INPUT) }

		//buffer out
		uint32_t buffMaxSize = 64;
		buff = malloc(buffMaxSize); if (!buff) { ERROR(ERR_MEMORY_NOT_ENOUGH) } memset(buff, 0, buffMaxSize);
		char* ptrBuff = buff;

		//table
		SqlCmdTag* tagg = ptrBuff;	ptrBuff += sizeof(SqlCmdTag);
		tagg->cmd = SQL_CMD_DROP;	tagg->size = tableLen;
		memcpy(ptrBuff, table, tableLen);	ptrBuff += tagg->size;

		*stream = buff;
		*streamSize = ptrBuff - buff;
	}
	else if (strncmp(sql, "show tables", 11) == 0) {
		//show tables
		uint32 len = sqlLen;
		char* tableList = getShowTablesList(sql, &len);

		if (tableList && len == 1) {
			if (tableList[0] == '*') {
				tableList = NULL;
				len = 0;
			}
		}

		//buffer out
		uint32_t buffMaxSize = sizeof(SqlCmdTag) + len;
		buff = malloc(buffMaxSize); if (!buff) { ERROR(ERR_MEMORY_NOT_ENOUGH) } memset(buff, 0, buffMaxSize);
		char* ptrBuff = buff;

		//table
		SqlCmdTag* tag = ptrBuff;	ptrBuff += sizeof(SqlCmdTag);
		tag->cmd = SQL_CMD_SHOW_TABLES;	tag->size = len;
		if(tableList)
			memcpy(ptrBuff, tableList, len);

		*stream = buff;
		*streamSize = buffMaxSize;
	}
	else if (strncmp(sql, "show columns", 12) == 0) {
		//show columns
		uint32 colLen = sqlLen;
		uint32 fromLen = sqlLen;
		char* colList = getShowColumnsList(sql, &colLen);
		char* from = getShowColumnsFrom(sql, &fromLen);

		if (!isValidTableName(from, fromLen)) {
			ERROR(ERR_INVALID_FROM);
		}

		if (colList && colLen == 1) {
			if (colList[0] == '*') {
				colList = NULL;
				colLen = 0;
			}
		}

		//buffer out
		uint32_t buffMaxSize = sizeof(SqlCmdTag)*2 + colLen + fromLen;
		buff = malloc(buffMaxSize); if (!buff) { ERROR(ERR_MEMORY_NOT_ENOUGH) } memset(buff, 0, buffMaxSize);
		char* ptrBuff = buff;

		//table
		SqlCmdTag* tag = ptrBuff;	ptrBuff += sizeof(SqlCmdTag);
		tag->cmd = SQL_CMD_SHOW_COLUMNS;	tag->size = colLen;
		if (colList) {
			memcpy(ptrBuff, colList, colLen); ptrBuff += colLen;
		}
		tag = ptrBuff;	ptrBuff += sizeof(SqlCmdTag);
		tag->cmd = SQL_CMD_FROM;	tag->size = fromLen;
		memcpy(ptrBuff, from, fromLen); ptrBuff += fromLen;

		*stream = buff;
		*streamSize = buffMaxSize;
	}
	else {
		ERROR(-2);
	}
	
	return 0;
error:
	switch (err)
	{
	case ERR_INVALID_INPUT:
		printf("Invalid input\n");
		break;
	case ERR_INVALID_SQL:
		printf("Invalid sql cmd, should input [select | update | insert | create | delete]\n");
		break;
	case ERR_INVALID_WHERE:
		printf("invalid where cmd\n");
		break;
	case ERR_INVALID_TABLE:
		printf("invalid table\n");
		break;
	case ERR_INVALID_TABLE_NAME:
		printf("invalid table name\n");
		break;
	case ERR_UNSUPPORT_SQL_IN_SHELL:
		printf("Unsupported type by shell\n");
		break;
	case ERR_MEMORY_NOT_ENOUGH:
		printf("buffer not enough\n");
	default:
		printf("unknown error\n");
		break;
	}
	free(buff);
	free(setBuff);
	stackFreeOperatorStack(&exp);
	free(*stream); 
	*stream = NULL;
	return err;

}

void showSqlStream(char* stream, uint32_t size) {
	int err = 0;
	if (stream == NULL)
		return;

	SqlCmdTag* tag = stream;
	uint32_t subSize = 0;
	char* ptr = tag+1;
	char* tmp = ptr;
	switch (tag->cmd)
	{
	case SQL_CMD_SELECT: {
		printf("select ");
		subSize = tag->size;
		ptr = tag + 1;
		printf("%.*s", tag->size, ptr);
		printf(" ");
		tag = ptr + tag->size;
		ptr = tag + 1;	if (ptr - stream > size) {ERROR(-1);}

		if (tag->cmd != SQL_CMD_FROM) {
			ERROR(-9);
		}
		ptr = tag + 1;
		printf("from %.*s",tag->size, ptr);
		
		tag = ptr + tag->size;
		ptr = tag + 1;
		if (ptr - stream >= size) {
			return 0;
		}
		//where
		if (tag->cmd != SQL_CMD_WHERE) {
			ERROR(-9);
		}
		printf(" where ");
		tmp = ptr;
		subSize = tag->size;
		showStackBuff(tmp, subSize);

		break;
	}
	case SQL_CMD_CREATE: {
		//create {table1: [col: *int32][col2: string(20)][col3: *float][col4: double][col5: image][col6: table]} 
		printf("create table %.*s ", tag->size, ptr);	ptr += tag->size;
		tag = ptr; ptr = tag + 1;
		if (tag->cmd != SQL_CMD_COL_INFO) { return; }
		uint32_t cnt = tag->size / sizeof(SqlColumnItem);
		SqlColumnItem* colItem = ptr;
		printf("(");
		for (uint32_t i = 0; i < cnt;i++, colItem++) {
			err = colInfoItem2Str(colItem); if (err < 0) { return; }
			if (i < cnt - 1)
				printf(", ");
		}
		printf(")\n");
		break;
	}
	case SQL_CMD_INSERT: {
		printf("insert into %.*s ",tag->size, tag+1);
		printf("(");
		ptr = tag + 1;
		tag = ptr + tag->size;
		if (tag->cmd != SQL_CMD_VALUE) { ERROR(-1); }
		SqlInsertColumnInfo* col = tag + 1;
		while ((char*)col - stream < size) {
			printm("%s=", col->name);
			char* data = col + 1;
			switch (col->type)
			{
			case SQL_DATA_STRING: {
				printf("'%.*s'", col->dataLen, data);
				break;
			}
			case SQL_DATA_INT8:
				printf("%d", *((int8_t*)data));
				break;
			case SQL_DATA_INT16:
				printf("%d", *((int16_t*)data));
				break;
			case SQL_DATA_TABLE:
			case SQL_DATA_INT32:
				printf("%d", *((int32_t*)data));
				break;
			case SQL_DATA_INT64:
				printf("%lld", *((int64_t*)data));
				break;
			case SQL_DATA_FLOAT:
				printf("%g", *((float*)data));
				break;
			case SQL_DATA_DOUBLE:
				printf("%g", *((double*)data));
				break;
			case SQL_DATA_BINARY:
			case SQL_DATA_IMAGE:
				printf("%.*s", col->dataLen, data);
				break;
			default:
				printf("[invalid!]");
				break;
			}
			col = data+col->dataLen;

			if ((char*)col - stream < size) {
				printm(", ");
			}
		}

		printf(")\n");
		break;
	}
	case SQL_CMD_UPDATE:
	{
		printf("update %.*s set ",tag->size, ptr);//table
		ptr += tag->size;//ptr->set
		tag = ptr;
		ptr = tag + 1;
		while (tag->cmd == SQL_CMD_SET && ptr - stream < size) {
			showStackBuff(ptr, tag->size);
			ptr += tag->size;
			tag = ptr;
			ptr = tag + 1;
			if (tag->cmd != SQL_CMD_SET) {
				printf(" ");
			}
			else {
				printf(", ");
			}
		}
		if (ptr - stream >= size)
			break;
		if (tag->cmd != SQL_CMD_WHERE)
			break;
		printf("where ");
		showStackBuff(ptr, tag->size);

		break;
	}
	case SQL_CMD_DELETE: {
		printf("delete from %.*s ",tag->size, ptr);//table
		ptr += tag->size;
		tag = ptr; ptr = tag + 1;
		if (tag->cmd == SQL_CMD_WHERE) {
			printf("where ");
			showStackBuff(ptr, tag->size);
		}
		
		break;
	}
	case SQL_CMD_DROP: {
		printf("drop table %.*s\n", tag->size, ptr);
		break;
	}
	case SQL_CMD_SHOW_TABLES: {
		if (tag->size) {
			printf("show tables %.*s\n", tag->size, ptr);
		}
		else {
			printf("show tables *\n");
		}
		break;
	}
	case SQL_CMD_SHOW_COLUMNS: {
		if (tag->size) {
			printf("show columns %.*s ", tag->size, ptr);
		}
		else {
			printf("show columns * ");
		}

		tag = ptr + tag->size;
		ptr = tag + 1;
		if (tag->cmd != SQL_CMD_FROM) { ERROR(-1); }
		printf("from %.*s\n", tag->size, ptr);
		break;
	}
	default:
		err = -9;
		break;
	}

	return;
error:
	switch (err)
	{
	case -1:
	case -2:
	case -9:
		printf("unknown cmd\n");
		break;
	default:
		break;
	}
	return;
}

uint32 sqlCmdGetColumnValueStrLen(char* value, uint32 size, sqlDataType type) {
	return getColValueCharLen(value, size, type);
}

void debug_sqlCmdByShell() {
	int err = 0;
	char select[256] = "select col1,col2 from table1 where userName < 'st r2' & (id = 6 | username2 = '123')";//"select col1,col2 from table1 where userName < 'str2' & ('col2' < 'col3' | id = 6)";
	char create[256] = "create table table1 ( col1 int8 index, col2 string(20), col3 float )";
	char update[256] = "update table1 set id = id+id2, id2 = 25 where id > id2";
	char insert[256] = "insert into table1 (userName, id,point) value ('str1', 123, 3.14)";
	char delete[256] = "delete from table2 where id > 80";
	char drop[256] = "drop table table1";


	char* cmdList[256] = {
		select, create, update, insert, delete, drop
	};

	for (uint32_t i = 0; i < 6;i++) {
		char* s = NULL;
		uint32_t size = 0;
		err = sqlCmdByShell(cmdList[i], strlen(cmdList[i]), &s, &size);
		if (err < 0) {
			printf("%s -> fail\n", cmdList[i]);
			return;
		}
		showSqlStream(s, size);
		printf(" -> pass\n");

		free(s);
	}
	printf("***all pass***\n");
	return;
}
void sqlCmd_test_charLen() {

	int64 num = 1312;
	uint32 len = getIntCharSize(num);
	
	printm("%lld len=%lu\n", num, len);

	double fnum = -3.1415926;
	len = getDoubleCharSize(fnum);
	printm("%g len=%lu\n",fnum, len);
}