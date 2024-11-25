#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vm.h"

#define PG_SIZE 4096
#define NR_PG 16

/* lru, pin, unpin, swap*/

int main(int argc, char *argv[]) {
  vmprint();
  char *ptr = malloc(NR_PG * PG_SIZE);
  printf("After malloc\n");
  vmprint();
  madvise((uint64) ptr + 3*PG_SIZE, PG_SIZE,  MADV_PIN); //pin 6
  printf("After madvise(MADV_PIN)\n");
  pgprint();

  madvise((uint64) ptr + 4*PG_SIZE, 3*PG_SIZE,  MADV_DONTNEED); //dontneed 7 8 9
  printf("After madvise(MADV_DONTNEED)\n");
  vmprint();
  pgprint();

  char *qtr;
  qtr = ptr + 5*PG_SIZE;
  *qtr = 'a'; //swap in 8
  printf("Page fault and swap in\n");
  vmprint();
  pgprint();

  madvise((uint64) ptr + 3*PG_SIZE, PG_SIZE,  MADV_UNPIN); //unpin 6
  printf("After unpin 6\n");
  vmprint();
  madvise((uint64) ptr + 3*PG_SIZE, PG_SIZE,  MADV_DONTNEED); //dontneed 6
  printf("After dontneed 6\n");
  vmprint();
  pgprint();

  madvise((uint64) ptr + 3*PG_SIZE, 4*PG_SIZE,  MADV_WILLNEED); //willneed 6 7 8 9
  printf("After madvise(MADV_WILLNEED) 6 7 8 9\n");
  vmprint();
  pgprint();

  //pin everything
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
