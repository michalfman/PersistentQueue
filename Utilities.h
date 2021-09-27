#ifndef UTILITIES_H_
#define UTILITIES_H_

#include <stddef.h> 			//for null
#include <climits>				//for max int
#include <fstream>

#define MAX_THREADS 144
#define FACTOR 100000
#define PADDING 512             // Padding must be multiple of 4 for proper alignment
#define QUEUE_SIZE 1000000
#define CAS __sync_bool_compare_and_swap
#define MFENCE __sync_synchronize

std::ofstream file;

void FLUSH(void *p) {
    asm volatile ("clflush (%0)" :: "r"(p));
}

void FLUSH(volatile void *p) {   
    asm volatile ("clflush (%0)" :: "r"(p));
}

void SFENCE() {
    asm volatile ("sfence" ::: "memory");
}

void BARRIER(void* p) {
	FLUSH(p);
	SFENCE();
}

void BARRIER(volatile void* p) {
	FLUSH(p);
	SFENCE();
}

void BARRIER_OPT(void* p) {
	FLUSH(p);
}

void BARRIER_OPT(volatile void* p) {
	FLUSH(p);
}

#endif /* UTILITIES_H_ */

