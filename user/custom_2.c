#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vm.h"

#define PG_SIZE 4096
#define NR_PG 16

/* madvise: lots of WILLNEED, DONTNEED, and page fault*/
uint rng() {
	static uint x = 41;
	return (x = x*0xdefaced + 17);
}

int main(int argc, char *argv[]) {
	vmprint();
	char *ptr = malloc(NR_PG * PG_SIZE);
	for (int i = 0;i < 255;i++) {
		int lef = rng()%15, rig = rng()%15;
		if (lef > rig) {
			int tmp = lef;
			lef = rig;
			rig = tmp;
		}
		int btype = rng()%3;
		if (btype == 0) {
			printf("DONTNEED %d %d\n", lef+3, rig+3);
			madvise((uint64) ptr + lef*PG_SIZE, (rig-lef+1)*PG_SIZE, MADV_DONTNEED);
		} else if (btype == 1) {
			printf("WILLNEED %d %d\n", lef+3, rig+3);
			madvise((uint64) ptr + lef*PG_SIZE, (rig-lef+1)*PG_SIZE, MADV_WILLNEED);
		}  else {
			int idx = rng()%15;
			char *qtr = ptr+idx*PG_SIZE+idx;
			printf("ACCESS %d\n", idx+3);
			*qtr = 'a';
		}
		vmprint();
	}
	exit(0);
}
