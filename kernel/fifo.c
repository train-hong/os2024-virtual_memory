#include "fifo.h"

#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "proc.h"

void q_init(queue_t *q){
	q->size = 0;
	for (int i = 0; i < PG_BUF_SIZE; i++) 
		q->bucket[i] = 0;
}

int q_push(queue_t *q, uint64 e){
	q->bucket[q->size] = e;
	q->size++;
	return 0;
}

uint64 q_pop_idx(queue_t *q, int idx){
	uint64 ret = q->bucket[idx];
	q->size--;
	for (int i = idx; i < q->size; i++) {
		q->bucket[i] = q->bucket[i+1];
	}
	q->bucket[q->size] = 0;
	return ret;
}

int q_empty(queue_t *q){
	return (q->size == 0);
}

int q_full(queue_t *q){
	return (q->size == PG_BUF_SIZE);
}

int q_clear(queue_t *q){
	for (int i = 0; i < PG_BUF_SIZE; i++) {
		q->bucket[i] = 0;
	}
	q->size = 0;
	return 0;
}

int q_find(queue_t *q, uint64 e){
	for (int i = 0; i < PG_BUF_SIZE; i++) {
		if (q->bucket[i] == e) {
			return i;
		}
	}
	return -1;
}
