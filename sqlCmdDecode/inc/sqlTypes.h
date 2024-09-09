#pragma once
#include "MyTypes.h"

#define TABLE_CNT_MAX (2048U)
#define TABLE_NAME_LEN_MAX (64U)
#define COLUMN_NAME_LEN_MAX (60U)

typedef enum sqlDataType_e {
	SQL_DATA_INVALID,
	SQL_DATA_INT8,
	SQL_DATA_INT16,
	SQL_DATA_INT32,
	SQL_DATA_INT64,
	SQL_DATA_TABLE,

	SQL_DATA_FLOAT,
	SQL_DATA_DOUBLE,

	SQL_DATA_STRING,
	SQL_DATA_IMAGE,
	SQL_DATA_BINARY,
	SQL_DATA_MAX,
}sqlDataType;

typedef enum sqlCmdType_e {
	SQL_CMD_SELECT,
	SQL_CMD_CREATE,
	SQL_CMD_UPDATE,
	SQL_CMD_DELETE,
	SQL_CMD_INSERT,
	SQL_CMD_DROP,

	SQL_CMD_FROM,
	SQL_CMD_SET,
	SQL_CMD_INTO,
	SQL_CMD_VALUE,
	SQL_CMD_COL_INFO,
	SQL_CMD_WHERE,

	SQL_CMD_SHOW_TABLES,
	SQL_CMD_SHOW_COLUMNS,

	SQL_CMD_MAX,
}SqlCmdType;

typedef struct SqlColumnItem_t {
	char name[COLUMN_NAME_LEN_MAX];
	union {
		uint32 dummy;
		struct {
			uint32 super : 1;
			uint32 single : 1;
			uint32 index : 1;
			uint32 notNull : 1;
			uint32 unique : 1;
			uint32 dataType : 4; //sqlDataType
			uint32 strMaxLen : 32 - 9;//will be use only string type 
		};

	};
}SqlColumnItem;

typedef struct TableInfo_t {
	uint64 id;
	char name[TABLE_NAME_LEN_MAX];
	uint32 colNum;
	SqlColumnItem colInfo[0];
}TableInfo;

typedef struct sqlCmdTag_t {
	union
	{
		uint32 dummy;
		struct {
			uint32 cmd : 4; //sqlCmdType
			uint32 size : 32 - 4;//不包含自己
		};
	};
}SqlCmdTag;

typedef struct SqlInsertColumnInfo_t {
	char name[COLUMN_NAME_LEN_MAX];
	union {
		uint32 dummy;
		struct {
			uint32 type : 4; //sqlDataType
			uint32 dataLen : 32 - 4;
		};
	};
}SqlInsertColumnInfo;