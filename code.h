#ifndef _CODE_H_
#define _CODE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

//various errors during memory allocation
#define ERR_SPLIT_NODE_NO_SIZE      0x1     
#define ERR_MEMORY_OUT_OF_BOUNDS    0x2
#define ERR_NODE_MAGIC_FAILED       0x4

// initialize our memory by requesting OS for bulk memory
void t_initOSMemory(int32_t totalSize);

// release all memory
void t_clearOSMemory();

// print stats on gMemory
void t_logMemoryStats();

// t_malloc
void *t_malloc(int32_t size);

// t_free
int32_t t_free(void *mem);

//debug functions
void t_log_enable(int8_t enable);
void t_log(char *format,...);

#ifdef __cplusplus
};
#endif

#endif //_CODE_H_

