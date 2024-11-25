#include "lru.h"

#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "proc.h"

void lru_init(lru_t *lru){
	lru->size = 0;
	for (int i = 0; i < PG_BUF_SIZE; i++) 
		lru->bucket[i] = 0;
}

int lru_push(lru_t *lru, uint64 e){
	lru->bucket[lru->size] = e;
	lru->size++;
	return 0;
}

uint64 lru_pop(lru_t *lru, int idx){
	if (idx == -1) {
		// pop the most likely victim that is not pinned
	}
	uint64 ret = lru->bucket[idx];
	lru->size--;
	for (int i = idx; i < lru->size; i++) {
		lru->bucket[i] = lru->bucket[i+1];
	}
	lru->bucket[lru->size] = 0;
	return ret;
}

int lru_empty(lru_t *lru){
	return (lru->size == 0);
}

int lru_full(lru_t *lru){
	return (lru->size == PG_BUF_SIZE);
}

int lru_clear(lru_t *lru){
	for (int i = 0; i < PG_BUF_SIZE; i++) {
		lru->bucket[i] = 0;
	}
	lru->size = 0;
	return 0;
}

int lru_find(lru_t *lru, uint64 e){
	for (int i = 0; i < PG_BUF_SIZE; i++) {
		if (lru->bucket[i] == e) {
			return i;
		}
	}
	return -1;
}
