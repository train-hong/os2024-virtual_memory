#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/vm.h"

#define PG_SIZE 4096
#define NR_PG 4096

/*large number of pages allocated*/

int main(int argc, char *argv[]) {
  	vmprint();
	for (int i = 0;i < 2;i++) {
  		char *ptr = malloc(NR_PG * PG_SIZE);
  		vmprint();
	}
  	exit(0);
}
