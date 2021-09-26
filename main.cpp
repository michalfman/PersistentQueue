#include <pthread.h>
#include <iostream>
#include <cstdlib>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#include <sys/time.h>

#include "MSQueue.h"
#include "DurableQueue.h"
#include "LogQueue.h"
#include "RelaxedQueue.h"
#include "Utilities.h"

#define ADD __sync_fetch_and_add
#define BASIC 1

pthread_t threads[MAX_THREADS];
int arguments[MAX_THREADS * PADDING];
int numThreads = 2;
int timeForRecord = 5;
bool run = false, stop = false;

MSQueue<int> msQueue;
int totalNumMSQueueActions = 0;

DurableQueue<int> durableQueue;
int totalNumDurableQueueActions = 0;

LogQueue<int> logQueue;
int totalNumLogQueueActions = 0;

RelaxedQueue<int> relaxedQueue;
int totalNumRelaxedActions = 0;
int totalNumSyncActions = 0;

//====================================Start MSQueue Test====================================

void* startRoutineMSQueue(void* argsInput) {

    long numMyOps=0;

    MSQueue<int>& queue = msQueue;
    int i = *(int*)argsInput;
    unsigned int seed = i + 1;

    while (run == false) {            // busy-wait to start "simultaneously"
        MFENCE();
        pthread_yield();
    }

    while(!stop){
        numMyOps+=2;
        queue.enq(i);
        queue.deq();
    }
    ADD(&totalNumMSQueueActions, numMyOps);

    return 0;
}


void countMSQueue() {

    msQueue.initialize();

    run = false;
    stop = false;

    for (int i = 0; i < numThreads; i++) {
	arguments[i * PADDING] = i;
	if(pthread_create(&threads[i], nullptr, startRoutineMSQueue, (void*)&arguments[i * PADDING])){
	    cout << "Error occurred when creating thread" << i << endl;
	    exit(1);
	}
    }

    run = true;
    MFENCE();
    sleep(timeForRecord);
    stop = true;
    MFENCE();

    for (int i = 0; i < numThreads; i++) {
	pthread_join(threads[i], nullptr);
    }

    cout << totalNumMSQueueActions/timeForRecord << endl;
    file << totalNumMSQueueActions/timeForRecord << endl;
}


//=======================================End MSQueue Test======================================


//===================================Start DurableQueue Test===================================


void* startRoutineDurable(void* argsInput){

    long numMyOps=0;

    DurableQueue<int>& queue = durableQueue;
    int i = *(unsigned int*)argsInput;
    unsigned int seed = i + 1;

    while (run == false) {            // busy-wait to start "simultaneously"
        MFENCE();
        pthread_yield();
    }

    while(!stop){
        numMyOps+=2;
        queue.enq(i);
        queue.deq(i);
    }
    ADD(&totalNumDurableQueueActions, numMyOps);

    return 0;
}


void countDurable() {

    durableQueue.initialize();

    run = false;
    stop = false;


    //lock free queue
    for (int i = 0; i < numThreads; i++) {
        arguments[i * PADDING] = i;
	if(pthread_create(&threads[i], NULL, startRoutineDurable, (void*)&arguments[i * PADDING])) {
	    cout << "Error occurred when creating thread" << i << endl;
	    exit(1);
	}
    }

    run = true;
    MFENCE();
    sleep(timeForRecord);
    stop=true;
    MFENCE();

    for (int i = 0; i < numThreads; i++) {
	pthread_join(threads[i], NULL);
    }

    file << totalNumDurableQueueActions/timeForRecord << endl;
    cout << totalNumDurableQueueActions/timeForRecord << endl;
}

//=====================================End DurableQueue Test===================================


//=======================================Start LogQueue Test====================================

void* startRoutineLog(void* argsInput){

    long numMyOps=0;

    LogQueue<int>& queue = logQueue;
    int i = *(int*)argsInput;
    unsigned int seed = i + 1;

    while (run == false) {            // busy-wait to start "simultaneously"
        MFENCE();
        pthread_yield();
    }

    while(!stop){
        numMyOps+=2;
        queue.enq(i, i, i);
        queue.deq(i, i);
    }
    ADD(&totalNumLogQueueActions, numMyOps);


    return 0;
}



void countLog(){
    
    logQueue.initialize();
    
    run = false;
    stop = false;


    //lock free queue
    for (int i = 0; i < numThreads; i++) {
        arguments[i * PADDING] = i;
        if(pthread_create(&threads[i], NULL, startRoutineLog, (void*)&arguments[i * PADDING])){
            cout << "Error occurred when creating thread" << i << endl;
            exit(1);
        }
    }

    run = true;
    MFENCE();
    sleep(timeForRecord);
    stop=true;
    MFENCE();

    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }

    file << totalNumLogQueueActions/timeForRecord << endl;
    cout << totalNumLogQueueActions/timeForRecord << endl;

}

//=============================================End LogQueue Test=======================================


//==========================================Start RelaxedQueue Test====================================


void* startRoutineRelaxedQueue(void* argsInput){
    
    long numMyOps=0;
    long numMySyncs = 0;
    
    RelaxedQueue<int>& queue = relaxedQueue;
    int i = *(unsigned int*)argsInput;
    unsigned int seed = 1;
    
    while (run == false) {            // busy-wait to start "simultaneously"
        MFENCE();
        pthread_yield();
    }
    
    while(!stop){
        numMyOps+=2;
        queue.enq(0);
        queue.deq();
        if(numMyOps % i == 0){
            numMySyncs ++;
            queue.sync(0);
        }
    }
    ADD(&totalNumRelaxedActions, numMyOps);
    ADD(&totalNumSyncActions, numMySyncs);
    return 0;
}

void countRelaxed(int f){
    
    relaxedQueue.initialize();
    int frequency = f;
    
    relaxedQueue.sync(0);
    
    run = false;
    stop = false;
    
    totalNumRelaxedActions = 0;
    totalNumSyncActions = 0;
    
    
    for (int i=0; i < numThreads; i++) {
        arguments[i * PADDING] = frequency;
        if(pthread_create(&threads[i], NULL, startRoutineRelaxedQueue, (void*)&arguments[i * PADDING])){
            cout << "Error occurred when creating thread" << i << endl;
            exit(1);
        }
    }
    
    
    run = true;
    MFENCE();
    sleep(timeForRecord);
    stop=true;
    MFENCE();
    
    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    file << totalNumRelaxedActions/timeForRecord << endl;
    cout << "Throughput : " << totalNumRelaxedActions/timeForRecord << endl;
    cout << "Num of syncs : " << totalNumSyncActions/timeForRecord << endl;
    return;
    
}


//==============================================End RelaxedQueue Test=====================================


//====================================================================================================

/* The main can run all the queue versions. It requires the following command line parameters:
 * 1 - The test num. 1 is the original Michael and Scott's lock free queue.
 *     2 is the Durable queue. 3 is the Log queue. 4 is the relaxed queue which is also
 *     optimizaed for big sizes of queues.
 * 2 - the number of the running threads.
 * 3 - the frequency of calling to sync for every thread. It is related only to test 4. All the
 *     rest should get the default number of 1, but they do not use it anyway.
 * 4 - the iteration number. Prints the test name only for the first iteration.
 * 5 - the size of the queue. Makes a difference only for the relaxed queue. Tests 1-3 expects to get a
 *     relatively small size of queue which is picked here as 5. If they get bigger sizes, they ignore it.
 *     Test number 4 can get any size and is actually influenced by this parameter.
 */ 
int main(int argc, char* argv[]){

    file.open("results.txt", ofstream::app);

    int testNum = atoi(argv[1]);
    numThreads = atoi(argv[2]);
    int frequency = atoi(argv[3]);
    int iteration = atoi(argv[4]);
    int size = atoi(argv[5]);

    // The "frequency" is related only to test number 4 which
    // presents different versions of the relaxed queue
    if (frequency > 1) {
    	if (testNum != 4) {
            return 0;
      	}
    }
    // The "size" is related only to test number 4 which
    // presents the version of the big relaxed queue that have a more optimized version
    // for taking a snapshot
    if (size != 5 && testNum != 4) {
	return 0;
    }

    if (testNum == 1) {
        if (iteration == 1) {
	    file << "Test MSQueue - Threads num: " << numThreads << endl;
	    cout << "Test MSQueue - Threads num: " << numThreads << endl;
	}
	countMSQueue();
    } else if (testNum == 2){
	if (iteration == 1) {
	    file << "Test Durable - Threads num: " << numThreads << endl;
	    cout << "Test Durable - Threads num: " << numThreads << endl;
	}
	countDurable();
    } else if(testNum == 3) {
	if (iteration == 1) {
	    file << "Test Log - Threads num: " << numThreads << endl;
	    cout << "Test Log - Threads num: " << numThreads << endl;
	}
	countLog();
    } else if (testNum == 4) {
        if (iteration == 1) {
            file << "Test Relaxed - Threads num: " << numThreads << " Frequency: "<< numThreads * frequency << " Size: " << size << endl;
            cout << "Test Relaxed - Threads num: " << numThreads << " Frequency: "<< numThreads * frequency << " Size: " << size << endl;
        }
        countRelaxed(numThreads * frequency);
    }
    return 0;
}


