#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "proc.h"

/* NTU OS 2024 */
/* Allocate eight consecutive disk blocks. */
/* Save the content of the physical page in the pte */
/* to the disk blocks and save the block-id into the */
/* pte. */
char *swap_page_from_pte(pte_t *pte) {
  char *pa = (char*) PTE2PA(*pte);
  uint dp = balloc_page(ROOTDEV);

  write_page_to_disk(ROOTDEV, pa, dp); // write this page to disk
  *pte = (BLOCKNO2PTE(dp) | PTE_FLAGS(*pte) | PTE_S) & ~PTE_V;

  return pa;
}

/* NTU OS 2024 */
/* Page fault handler */
int handle_pgfault() {
  /* Find the address that caused the fault */
  /* TODO */
  uint64 va = PGROUNDDOWN(r_stval());
  pte_t *pte = walk(myproc()->pagetable, va, 0);

  if (pte == 0) {
    panic("handle_pgfault: walk failed");
  }
  // printf("handle_pgfault walk succeed, va = %p\n", va);

  if (*pte & PTE_S) {

    uint blockno = PTE2BLOCKNO(*pte);
    char *pa = kalloc();
    *pte = PA2PTE(pa) | PTE_FLAGS(*pte);
    memset((void *)PTE2PA(*pte), 0, PGSIZE);
    begin_op();
    read_page_from_disk(ROOTDEV, PTE2PA(*pte), blockno);
    bfree_page(ROOTDEV, blockno);
    end_op();
    *pte = (*pte | PTE_V) & ~PTE_S;

  } else {
    void *pa = kalloc();
    if (pa == 0)
      panic("handle_pgfault(): kalloc() fault");
    memset((void *)pa, 0, PGSIZE);

    int perm = PTE_W | PTE_R | PTE_X | PTE_U;
    mappages(myproc()->pagetable, va, PGSIZE, (uint64)pa, perm);
    // printf("mappages done\n");
  }

  return 0;
}
