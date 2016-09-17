#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include "code.h"

// instance of test
typedef struct _testInit {
    int32_t testId;
    int32_t totalSize;      //total size to be allocated
    int32_t memObj[256];    //different objects, each with object size
    int32_t memOps[1024];   // (+/-) (malloc/free) operations on above objects
}testStruct;

// given Obj and Ops, do test
void doTest(int32_t tId, int32_t memObj[], int32_t numObjs, 
    int32_t memOps[], int32_t numOps){

    void ** ObjPtrs;
    ObjPtrs = (void*)malloc(numOps*sizeof(void*));
    void * ptr = NULL; 
    for (int i=0;i <numOps;i++){
        int32_t isMalloc = (memOps[i] >0)?1:0;
        int32_t objId = abs(memOps[i])-1;
        int32_t objSize = memObj[objId];
        if(isMalloc) {
            void *ptr = t_malloc(objSize);
            if (ptr){
                // use memory
                *((int*)ptr) = i+200;
                ObjPtrs[objId] = ptr;
            }
            t_log("Tid#(%d)::Ops#(%d)::Malloc Ops/size(%d/%d)::Status=(%s)\n", 
                tId, i, memOps[i], objSize,(ptr)?"success":"fail");
        }else{
            //free operation
            void * ptr = ObjPtrs[objId];
            int32_t ret = t_free(ptr);
            t_log("Tid#(%d)::Ops#(%d)::Free Ops/size(%d/%d)::Status=(%s)\n", 
                tId,i, memOps[i],objSize,(!ret)?"success":"fail");
        }
    }
    free(ObjPtrs);
}

// various tests
testStruct tests[10] = 
{
    { 1, 20*1024,           // test serialized alloc/free
      { 4, 4, 8, 256, 16 }, 
      { 1, 2,3,4, 5,-1,-2,-3,-4,-5 }
    },
    { 2, 20*1024,           // test join node both sides
      { 16,256,60 },
      { 1,2,3,-1,-3,-2}
    },
    { 3, 1*1024,            // two malloc failures
      { 16,16,256,256,256,256},
      { 1,2,3,4,5,6,-5,6,5,-6,-4,-3,-2,-1}
    }, 
}; 

// auto-initialization by compiler fills zeros. 
// so count the actual instances
int countNonZeros(int32_t obj[]){
    int i=0;
    while(obj[i]) i++;  
    return i;
}

// thread function needs context
// todo - combine serialized and thread tests
typedef struct _threadTest{
    int32_t threadId;
    int32_t memObj[256];    //maximum number of objects, with object size
    int32_t memOps[1024];   //operations on these objects
    int32_t numObjs;
    int32_t numOps;
}threadTest;

//thread runnable function
void *threadRun(void* context){
    threadTest *ourCtx = (threadTest*) context;
    doTest(ourCtx->threadId, ourCtx->memObj, ourCtx->numObjs,
        ourCtx->memOps, ourCtx->numOps);
}


// test routines
// todo - add gTest like different sections
int main(int argc, char ** argv){

    // todo - usage, etc. 
    printf("%d-argc, %s-argv\n",argc,argv[0]);
    t_log_enable(1);


    // example usage
    t_initOSMemory(20*1024);
    void *ourPtr = NULL;
    ourPtr = t_malloc(200);   // malloc 200 bytes
    if (ourPtr){
        t_log("Malloc successful\n");
        t_free(ourPtr);
        t_log("Free succesful\n");
    }
    t_logMemoryStats();
    t_clearOSMemory();


    // simple unit tests to cover basic conditions
    for (int i= 0; i<sizeof(tests);i++){
        if(!tests[i].totalSize)
            break;
        t_log(" ======================================================\n");
        t_log(" ==========    Start Test[%d]  ==========\n", tests[i].testId);
        
        int32_t numObjs = countNonZeros(tests[i].memObj);
        int32_t numOps = countNonZeros(tests[i].memOps);
        t_initOSMemory(tests[i].totalSize);
        t_log("======== Start TestID (%d), Totalsize (%d), numObjs(%d), numOps (%d)\n",
            tests[i].testId, tests[i].totalSize, numObjs, numOps);
        doTest(tests[i].testId, tests[i].memObj, numObjs,
            tests[i].memOps, numOps);
        t_log("======== End  TestId(%d)========)\n",tests[i].testId);
        t_logMemoryStats();
        t_clearOSMemory();
        t_log(" ==========    End   Test[%d]  ==========\n", tests[i].testId);
    }

    // thread tests
    // todo
    // random allocation of number of objs between 5 to 50
    // random size allocation of each of these objs between 4 and 1024
    // random allocation of series of Ops, which malloc/free (in that sequence)
    // for each of the objs. 
     
    threadTest tTests = {
        1, 
        { 4, 4, 8, 256, 16 },
        { 1,2,3,4,5,-1,-2,-3,-4,-5,1,2,3,-3,-2,4,5,-1,-4,-5,1,2,3,4,5,-1,-2,-3,-4,-5,1,2,3,-3,-2,4,5,-1,-4,-5}
    };
    tTests.numObjs = countNonZeros(tTests.memObj);
    tTests.numOps = countNonZeros(tTests.memOps);
    pthread_t tid[10];
    int32_t numThreads=10;
    
    t_log(" ======================================================\n");
    t_log(" ==========    Start Thread Test  ==========\n");
    t_initOSMemory(20*1024);
    t_log("===Totalsize (%d), numObjs(%d), numOps (%d), numThreads(%d)\n",
            20*1024, tTests.numObjs, tTests.numOps,numThreads);
    t_logMemoryStats();

    int32_t i=0;
    while(i<numThreads){
        tTests.threadId = i;
        assert(!pthread_create(&(tid[i]), NULL, &threadRun, &tTests));
        i++;
    }
    i=0;

    t_log_enable(0);   //printf-stdout issue
    while(i<numThreads){
        pthread_join(tid[i],NULL);
        i++;
    }
    t_log_enable(1);
    t_log("======== End  Thread Test ==========\n");
    t_logMemoryStats();
    t_clearOSMemory();
}

