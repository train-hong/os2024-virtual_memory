#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vm.h"

#define PG_SIZE 4096
#define NR_PG 16

/* madvise: rules about pin and unpin*/

int main(int argc, char *argv[]) {
	vmprint();
	char *ptr = malloc(NR_PG * PG_SIZE);
	printf("After malloc\n");
	vmprint();

	madvise((uint64) ptr + 7*PG_SIZE, PG_SIZE,  MADV_PIN); //pin 10
	printf("After madvise(MADV_PIN 10)\n");
	vmprint();

	madvise((uint64) ptr + 4*PG_SIZE, 3*PG_SIZE,  MADV_DONTNEED); //dontneed 7 8 9
	printf("After madvise(MADV_DONTNEED 7 8 9)\n");
	vmprint();

	madvise((uint64) ptr + 7*PG_SIZE, PG_SIZE,  MADV_UNPIN); //unpin 10
	printf("After madvise(MADV_UNPIN 10)\n");
	vmprint();

	char *qtr = ptr + 5*PG_SIZE;
	*qtr = 'a'; //swap in 8
	printf("Page fault and swap in\n");
	vmprint();

	madvise((uint64) ptr + 7*PG_SIZE, PG_SIZE,  MADV_DONTNEED); //dontneed 10
	printf("After madvise(MADV_DONTNEED 10)\n");
	vmprint();

	qtr = ptr + 7*PG_SIZE;
	*qtr = 'a'; //swap in 10
	*(qtr+1) = 'b'; 
	printf("Page fault and swap in\n");
	vmprint();
	
	//memory persistence
	madvise((uint64) ptr + 7*PG_SIZE, PG_SIZE,  MADV_DONTNEED); //dontneed 10
	printf("After madvise(MADV_DONTNEED 10)\n");
	vmprint();
	*(qtr+2) = 'c';
	*(qtr+3) = 0;
	printf("Page fault and swap in\n");
	vmprint();
	printf("Saved string: %s\n", qtr);

	madvise((uint64) ptr + 7*PG_SIZE, PG_SIZE,  MADV_DONTNEED); //dontneed 10
	printf("After madvise(MADV_DONTNEED 10)\n");
	vmprint();
	madvise((uint64) ptr + 7*PG_SIZE, PG_SIZE,  MADV_WILLNEED); //dontneed 10
	printf("After madvise(MADV_WILLNEED 10)\n");
	vmprint();

	*(qtr+3) = 'd';
	*(qtr+4) = 0;
	printf("Saved string: %s\n", qtr);

	exit(0);
}
