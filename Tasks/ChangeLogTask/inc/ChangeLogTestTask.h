
/*
连续64k条写入命令，不会触发前台checkpoint。
128k会触发。
STORAGE_COMMAND_QUEUE_MAX_SIZE == 64
*/

#define DEBUG_CHANGE_LOG_TEST

TaskState ChangeLogTestTask(void* inP);
bool ChangeLogTestTaskMessage(void* parent);