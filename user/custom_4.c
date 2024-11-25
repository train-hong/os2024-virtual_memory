#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vm.h"

#define PG_SIZE 4096
#define NR_PG 16

/* fifo, pin, unpin, swap*/

int main(int argc, char *argv[]) {
	vmprint();
	char *ptr = malloc(NR_PG * PG_SIZE);
	printf("After malloc\n");
	vmprint();
	madvise((uint64) ptr + 3*PG_SIZE, PG_SIZE,  MADV_PIN); //pin 6
	printf("After madvise(MADV_PIN 6)\n");
	pgprint();

	madvise((uint64) ptr + 12*PG_SIZE, PG_SIZE,  MADV_PIN); //pin 15
	printf("After madvise(MADV_PIN 15)\n");
	pgprint();

	madvise((uint64) ptr + 10*PG_SIZE, PG_SIZE,  MADV_PIN); //pin 13
	printf("After madvise(MADV_PIN 13)\n");
	pgprint();

	char *qtr = malloc(2*PG_SIZE);
	printf("After malloc\n");
	vmprint();
	pgprint();

	madvise((uint64) ptr + 10*PG_SIZE, PG_SIZE,  MADV_UNPIN); //unpin 13
	printf("After madvise(MADV_UNPIN 12)\n");
	pgprint();

	madvise((uint64) ptr + 10*PG_SIZE, PG_SIZE,  MADV_DONTNEED); //dontneed 13
	printf("After madvise(MADV_DONTNEED 13)\n");
	vmprint();
	pgprint();

	qtr = ptr + 10*PG_SIZE;
	*qtr = 'a'; //swap in 13
	printf("Page fault and swap in\n");
	vmprint();
	pgprint();

	madvise((uint64) ptr + 3*PG_SIZE, PG_SIZE,  MADV_UNPIN); //unpin 6
	vmprint();
	madvise((uint64) ptr + 3*PG_SIZE, PG_SIZE,  MADV_DONTNEED); //dontneed 6
	vmprint();
	pgprint();

	for (int i = 16;i >= 9;i--) {
		madvise((uint64) ptr + (i-3)*PG_SIZE, PG_SIZE,  MADV_PIN);
		printf("After madvise(MADV_PIN) %d\n", i);
		vmprint();
		pgprint();
	}

	madvise((uint64) ptr + 14*PG_SIZE, PG_SIZE,  MADV_WILLNEED); //willneed 17
	printf("After madvise(MADV_WILLNEED) 17\n");
	vmprint();
	pgprint();

	exit(0);
}
