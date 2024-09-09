#pragma once
#include <stdint.h>
#include "myStack.h"
#include "sqlCmdDecode.h"
#include "operatorTypeDef.h"
#include "sqlTypes.h"

#ifndef SQL_COL_NAME_LEN_MAX
#define SQL_COL_NAME_LEN_MAX (UINT8_MAX-1)
#endif



uint32_t getOpLevel(operatType op);
uint32_t getNextSubStrLenBeforeComma(char* str, uint32_t len);
uint32_t getNextSubStrLenBeforeSpace(char* str, uint32_t len);
char* getNextNotSpaceChar(char* str, uint32_t len);
uint8_t isValidOperator(char* op);
uint8_t isValidColName(char* colName, uint32_t len);
uint8_t isValidSetExpression(char* exp, uint32_t len);
char* getNextOperation(char* exp, uint32_t len);
char* getNextSingleQuotes(char* str, uint32_t len, uint32_t* outLen);
char* getNexBracket(char* str, uint32_t len, uint32_t* outLen);
char* getNextBracketMultipleExpression(char* str, uint32_t len, uint32_t* outLen);

int calculateInt(int num1, int num2, operatType op);
int calculateIntEquation(int64_t num1, int64_t num2, operatType op);
unsigned int calculateBoolEquation(unsigned int bool1, unsigned int bool2, operatType op);
uint32_t getEqualCnt(stackNode* stack, uint32_t cnt);
stackNode* getNextEqualNode(stackNode* stack, uint32_t cnt, uint32_t* index);
stackNode* getLastAndOr(stackNode* stack, uint32_t cnt, stackNode* node);

uint32_t varNodeCntInSubStack(stackNode** head, uint32_t cnt);
int removeUnuselessBracketsInStr(char* str, uint32_t len, uint32_t* newLen);
int removeUnuselessBracketsInStack(stackNode** head);
double calculateDouble(double num1, double num2, operatType op);
int str2Double(char* str, uint32_t len, uint8_t* isMinus, double* outNum);
int str2Int64(char* str, uint32_t len, uint8_t* isMinus, int64_t* outNum);
int sqlStrExpressionToStack(char* tableName, char* str, uint32_t len, stackNode** head);


void showStackBuff(char* buff, uint32_t size);
void showStackNode(stackNode* node, char* value);
void stackShow(stackNode* stack);
void stackShowByCnt(stackNode* stack, uint32_t cnt);

void stackFreeOperatorStack(stackNode** stack);


int singleExpressionSimplify(stackNode** head, uint32_t cnt, uint32_t* newCnt);
int multipleExpressionSimplify(stackNode** head, uint32_t cnt, uint32_t* newCnt);

int calculateInt(int num1, int num2, operatType op);
int calculateIntEquation(int64_t num1, int64_t num2, operatType op);
int calculateDoubleEquation(double num1, double num2, operatType op);
int calculateBinaryEquation(char* binary1, uint32_t len1, char* binary2, uint32_t len2, operatType op);
int calculateStringEquation(char* str1, uint32_t len1, char* str2, uint32_t len2, operatType op);
unsigned int calculateBoolEquation(unsigned int bool1, unsigned int bool2, operatType op);