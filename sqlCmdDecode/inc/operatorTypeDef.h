#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define VALUE_INFO_VALUETYPE_BIT (3)
#define VALUE_INFO_OPTYPE_BIT (4)
#define VALUE_INFO_DATATYPE_BIT (4)
#define VALUE_INFO_BOOL_BIT (1)

/*
Plus sign
Minus sign
Multiplication sign
Division sign
Greater than sign
Less than sign
Greater than or equal to sign
Less than or equal to sign
Equal to sign
or
Related to
Left bracket
Right bracket
*/
typedef enum operatType_e {
	OT_Invalid,
	OT_Plus,					// +
	OT_Minus,					// -
	OT_Multiplication,			// *
	OT_Division,				// /
	OT_Greater_than,			// >
	OT_Less_than,				// <
	OT_Greater_than_or_equal,	// >=
	OT_Less_than_or_equal,		// <=
	OT_Equal,					// ==
	OT_Or,						// ||
	OT_And,						// &&
	OT_Left_bracket,			// (
	OT_Right_bracket,			// )
	OT_MAX
}operatType;

typedef enum valueType_e {
	VT_operator,
	VT_num,
	VT_var,
	VT_binary,
	VT_Bool,
	VT_Unkown//sub stack list
}valueType;

typedef struct valueInfo_t {
	uint32_t valueType : VALUE_INFO_VALUETYPE_BIT;//valueType
	uint32_t opType : VALUE_INFO_OPTYPE_BIT;//when operator
	uint32_t dataType : VALUE_INFO_DATATYPE_BIT;//when number
	uint32_t isTrue : VALUE_INFO_BOOL_BIT;//when bool
}valueInfo;