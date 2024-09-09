#pragma once
#include <stdint.h>
#include "myStack.h"

int expressionWhereCmd(char* table, char* cmd, uint32_t cmdLen, char** outStream, uint32_t* size);
