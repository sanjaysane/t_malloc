#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include "code.h"

// compile-time array of various sized bins
// we'll restrict max memory that can be requested to be equal to
// highest bin Size
// we allocate nodes (shrink them) to fit these bin sizes
int32_t binArray[] = {16,32,64,128,256,1024};
#define maxBins()       (sizeof(binArray)/sizeof(int32_t))
#define maxMemory()     (binArray[maxBins()-1])

// get nearest bin Size, bins are sorted ascending
int32_t nearestBinSize(int32_t memSize){
    int i;
    for(i=0; i<maxBins();i++){
        if(memSize <= binArray[i])
            return binArray[i];
    }
    return binArray[i-1];
}

//debugging functions
int32_t pDebug = 0;

void t_log_enable(int8_t enable){
    pDebug = enable;
}

void t_log(char *format,...) { 
    if(pDebug) {
        va_list args;
        va_start(args,format);
        vprintf(format,args);
        va_end(args);
    }
}

//our memory node, embedded in the allocated memory space
//and precedes actual memory provided to end-user
typedef struct _node {
    struct _node *next;
    struct _node *prev;  
    int32_t nodeSize;     // includes this header size as well
    int32_t flags;
}memNode;

#define overhead()      ((sizeof(memNode)))
#define nodeToMem(ptr)      ((void*)((uint8_t*)ptr+overhead()))
#define memToNode(ptr)      (memNode*)((uint8_t*)ptr-overhead())

//various flags for node
#define NODE_FREE      0x1
#define isNodeFree(node)      (node->flags & NODE_FREE)  
#define setNodeFree(node)     (node->flags |= NODE_FREE)
#define clearNodeFree(node)   (node->flags &= ~NODE_FREE)

//valid memory pointer must have this magic
#define NODE_MAGIC                 0xba << 16
#define isNodeMagic(node)          (node->flags & NODE_MAGIC)
#define setNodeMagic(node)         (node->flags |= NODE_MAGIC)

// global memory state 
typedef struct {
    int32_t totalSize;
    int32_t usedSize;
    pthread_mutex_t lock;
    int32_t totalOps;    
    memNode * pHead;
} theMem; 

// global data structure for memory state
theMem gMemory; 

// print stats on gMemory
void t_logMemoryStats(){
    t_log("totalSize(%d),usedSize(%d),first-node-size(%d),totalOps(%d)\n",
        gMemory.totalSize, gMemory.usedSize, gMemory.pHead->nodeSize,
        gMemory.totalOps);
}

// initialize our memory by requesting OS
void t_initOSMemory(int32_t totalSize){
    memset(&gMemory, 0, sizeof(gMemory));
    gMemory.pHead = (memNode*)mmap(NULL, totalSize, 
        PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON,0,0);
    assert(gMemory.pHead);
    gMemory.totalSize = totalSize;        
    gMemory.pHead->nodeSize = totalSize;
    setNodeMagic(gMemory.pHead);
    setNodeFree(gMemory.pHead);
    assert(!pthread_mutex_init(&gMemory.lock, NULL));
}

// release all memory
void t_clearOSMemory(){
    munmap(gMemory.pHead, gMemory.totalSize);
    pthread_mutex_destroy(&gMemory.lock);
    memset(&gMemory, 0, sizeof(gMemory));
}

// split existing node to nearest bin size
int32_t splitNode(memNode* node, int32_t memSize){
    //get nodeSize that we need to split
    int32_t nodeSize = nearestBinSize(memSize) + overhead();
    //split node , ensure we've space for next node overhead
    if (nodeSize <= node->nodeSize + overhead()){
        int32_t nextNodeSize = node->nodeSize - nodeSize;
        node->nodeSize = nodeSize;
        memNode *nextNode = (memNode*)((uint8_t*)node + nodeSize);
        nextNode->nodeSize = nextNodeSize;
        nextNode->next = node->next;
        node->next = nextNode;
        if(nextNode->next)
            nextNode->next->prev = nextNode;
        nextNode->prev = node;
        setNodeFree(nextNode);
        setNodeMagic(nextNode);
    } 
    else {return ERR_SPLIT_NODE_NO_SIZE;}
    return 0;
}

// get next free node that accomodates required memory
memNode * getFreeNode(memNode *beginNode, int32_t memSize){
    memNode *node = beginNode;
    while (node){
        if (isNodeFree(node) && (node->nodeSize >= (memSize+overhead())) )
            return node;
        node = node->next;
    }
    t_log("Could not find available node with size (%d)\n", memSize);
    return NULL;
}

// join neighboring empty nodes
void joinNode(memNode *node){

    // if my node is the only blank, its a no-op
    // check my previous node and next node, and join whoever is
    // blank
    // upon joining, ensure the correct nodeSize and reflect
    // next pointers for this new node, and back pointers as well. 

    if(node && node->prev && isNodeFree(node->prev)){
        if (node->next && isNodeFree(node->next)){
            // both prev and next are available
            node->prev->nodeSize += node->nodeSize + node->next->nodeSize;
            node->prev->next = node->next->next;
            if(node->next->next)
                node->next->next->prev = node->prev;
        }else{
            // only prev is available
            node->prev->nodeSize += node->nodeSize;
            node->prev->next = node->next;
            if(node->next)
                node->next->prev = node->prev;
        }
    }
    else if (node->next && isNodeFree(node->next)){
            // only next is available
            node->nodeSize += node->next->nodeSize;
            node->next = node->next->next;
            if(node->next)
                node->next->prev = node;
    }
}

// t_malloc
// finds next free node that is capable of handling requested size
// split this free node to nearest bin of requested size
void *t_malloc(int32_t size){
    if (size> maxMemory()) return NULL;
    pthread_mutex_lock(&gMemory.lock);
    memNode *node = getFreeNode(gMemory.pHead,size); 
    // todo, start from last allocated node instead of head 
    if(node){
        int ret = splitNode(node, size);
        assert(!ret);
        clearNodeFree(node);
        gMemory.usedSize += node->nodeSize;
    }
    gMemory.totalOps++;
    pthread_mutex_unlock(&gMemory.lock);
    return (node)? (void*)nodeToMem(node) : NULL;
}

// t_free
// free the node, and try to join neighboring nodes 
int32_t t_free(void *mem){
    if (!mem || mem < (void*)gMemory.pHead || 
        mem > ((void*)gMemory.pHead+gMemory.totalSize) )
        return ERR_MEMORY_OUT_OF_BOUNDS;
    // check node ptr is valid
    memNode *node = memToNode(mem);
    if (!node || !isNodeMagic(node))
        return ERR_NODE_MAGIC_FAILED;

    pthread_mutex_lock(&gMemory.lock);
    // free node
    setNodeFree(node);
    gMemory.usedSize -= node->nodeSize;
    // join node with neighbor nodes if they are free. 
    joinNode(node);
    gMemory.totalOps++;
    pthread_mutex_unlock(&gMemory.lock);
    return 0;
}        

