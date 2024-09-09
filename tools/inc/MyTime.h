#pragma once
#include <time.h>
#include "MyTypes.h"

#define DEBUG

time_t getCurTick();
uint64 myCounterStart();
uint64 myCounterUs(uint64 start);
uint64 getMicrosecondTick();
void printUsTick(uint64 us);	//print format us like: 13h 30m 28s 116us
void printUsToBuff(char* buff, uint32 len, uint64 us);

int tickToYMD(time_t tick, char* buff, uint32 len);	//need 45byte buffer
int tickToYMD2(time_t tick, char* buff, uint32 len);	//need 12byte buffer
bool TIME_Timeing(time_t startTime, int64 maxSec);	//if the time is up, return TRUE, else return FALSE
void mySleep(uint32 milliseconds);

void testTimeCounter();