#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "fifo.h"
#include "lru.h"
#include "inttypes.h"

// NTU OS 2024
// you may want to declare page replacement buffer here
// or other files
#ifdef PG_REPLACEMENT_USE_LRU
lru_t lru;
#elif defined(PG_REPLACEMENT_USE_FIFO)
queue_t q;
#endif

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // map kernel stacks
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }

  pte_t *pte = &pagetable[PX(0, va)];

  // NTU OS 2024
  // pte is accessed, so determine how
  // it affects the page replacement buffer here
  uint64 shifted_va = (va << 43) >> 55;
  if (!(shifted_va == 0 || shifted_va == 1 || shifted_va == 2)) {
    #ifdef PG_REPLACEMENT_USE_LRU
    // TODO
      int idx = lru_find(&lru, pte);
      if (idx != -1) {
        uint64 target = lru_pop(&lru, idx);
        lru_push(&lru, target);
      }
      else {
        int i = 0;
        if (lru_full(&lru)) {
          uint64 tmp = lru.bucket[i];
          while (*(pte_t *)tmp & PTE_P) {
            tmp = lru.bucket[++i];
            if (i == PG_BUF_SIZE) break;
          }
           if (i != PG_BUF_SIZE) lru_pop(&lru, i);
        }
        if (i != PG_BUF_SIZE) lru_push(&lru, pte);
      }
    #elif defined(PG_REPLACEMENT_USE_FIFO)
    // TODO
      int idx = q_find(&q, pte);
      if (idx == -1) {
        int i = 0;
        if (q_full(&q)) {
          uint64 tmp = q.bucket[i];
          while (*(pte_t *)tmp & PTE_P) {
            tmp = q.bucket[++i];
            if (i == PG_BUF_SIZE) break;
          }
          if (i != PG_BUF_SIZE) q_pop_idx(&q, i);
        }
        if (i != PG_BUF_SIZE) q_push(&q, pte);
      }

    #endif
  }
  return pte;
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size");
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");

    if(*pte & PTE_S) {
      /* NTU OS 2024 */
      /* int blockno = PTE2BLOCKNO(*pte); */
      /* bfree_page(ROOTDEV, blockno); */
      /* HACK: Do nothing here.
         The wait() syscall holds holds the proc lock and calls into this method.
         It causes bfree_page() to panic. Here we leak the swapped pages anyway.
      */
      continue;
    }

    if((*pte & PTE_V) == 0)
      continue;

    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

/* NTU OS 2024 */
/* Map pages to physical memory or swap space. */
int madvise(uint64 base, uint64 len, int advice) {
  struct proc *p = myproc();
  pagetable_t pgtbl = p->pagetable;

  if (base > p->sz || (base + len) > p->sz) {
    return -1;
  }

  if (len == 0) {
    return 0;
  }

  uint64 begin = PGROUNDDOWN(base);
  uint64 last = PGROUNDDOWN(base + len - 1);

  if (advice == MADV_NORMAL) {
    return 0;
  } else if (advice == MADV_WILLNEED) {

    for (uint64 va = begin; va <= last; va += PGSIZE) {
      pte_t *pte = walk(pgtbl, va, 0);

      if (pte != 0 && (*pte & PTE_S)) {
        // char *pa = (char *) PTE2PA(*pte);
        uint64 blockno = PTE2BLOCKNO(*pte);
        char *pa = kalloc();
        *pte = PA2PTE(pa) | PTE_FLAGS(*pte);
        memset((void *)PTE2PA(*pte), 0, PGSIZE);

        begin_op();
        read_page_from_disk(ROOTDEV, PTE2PA(*pte), blockno);
        bfree_page(ROOTDEV, blockno);
        end_op();

        *pte = (*pte | PTE_V) & ~PTE_S;
      }
      else if (pte != 0 && !(*pte & PTE_V)) {
        char *pa = kalloc();
        // *pte = PA2PTE((uint64)kalloc()) | PTE_W ;
        // *pte |= PTE_X;
        // *pte |= PTE_R;
        // *pte |= PTE_U;
        memset(pa, 0, PGSIZE);
        // *pte |= PTE_V;
        mappages(pgtbl, base, PGSIZE, (uint64)pa, PTE_W | PTE_X | PTE_R | PTE_U);
      }
    }
    return 0;
  } else if (advice == MADV_DONTNEED) {
    begin_op();

    pte_t *pte;
    for (uint64 va = begin; va <= last; va += PGSIZE) {
      pte = walk(pgtbl, va, 0);
      // printf("dontneed: %p\n",pte);
      if (pte != 0 && (*pte & PTE_V) && !(*pte & PTE_P)) {
        char *pa = (char*) swap_page_from_pte(pte);
        if (pa == 0) {
          end_op();
          return -1;
        }
        kfree(pa);

        // NTU OS 2024
        // Swapped out page should not appear in
        // page replacement buffer
        uint64 shifted_va = (va << 43) >> 55;
        if (!(shifted_va == 0 || shifted_va == 1 || shifted_va == 2)) {
          #ifdef PG_REPLACEMENT_USE_LRU
          // TODO
          int idx = lru_find(&lru, pte);
          lru_pop(&lru, idx);
          #elif defined(PG_REPLACEMENT_USE_FIFO)
          // TODO
          int idx = q_find(&q, pte);
          q_pop_idx(&q, idx);
          #endif
        }
      }
    }

    end_op();
    return 0;


  } else if(advice == MADV_PIN) {
    begin_op();
    
    pte_t *pte;
    for (uint64 va = begin; va <= last; va += PGSIZE) {
      pte = walk(pgtbl, va, 0);
      if (pte == 0) {
        panic("madvise PIN: walk failed");
        return -1;
      }
      *pte |= PTE_P;
    }

    end_op();
    return 0;
  } else if(advice == MADV_UNPIN) {
    begin_op();
    
    pte_t *pte;
    for (uint64 va = begin; va <= last; va += PGSIZE) {
      pte = walk(pgtbl, va, 0);
      if (pte == 0) {
        panic("madvise UNPIN: walk failed");
        return -1;
      }
      *pte &= ~PTE_P;
    }

    end_op();
    return 0;
  }
  else {
    return -1;
  }
}

/* NTU OS 2024 */
/* print pages from page replacement buffers */
#if defined(PG_REPLACEMENT_USE_LRU) || defined(PG_REPLACEMENT_USE_FIFO)
void pgprint() {
  printf("Page replacement buffers\n");
  printf("------Start------------\n");
  #ifdef PG_REPLACEMENT_USE_LRU
  // TODO
  for (int i = 0; i < lru.size; i++)
    printf("pte: %p\n", lru.bucket[i]);
  #elif defined(PG_REPLACEMENT_USE_FIFO)
  // TODO
  // printf("q.size = %d\n", q.size);
  for (int i = 0; i < q.size; i++)
  // for (int i = 0; i < PG_BUF_SIZE; i++)
    printf("pte: %p\n", q.bucket[i]);
  #endif
  printf("------End--------------\n");
}
#endif

void vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);

  /* level 2 */
  /* Find last entry of the page table */
  int last2 = -1;
  for (int i2 = 0 ; i2 < 512; i2++) {
    if (pagetable[i2] & PTE_V) {
      last2 = i2;
    }
  }

  for (uint64_t i2 = 0; i2 < 512; i2++) {
    pte_t *pte2 = &pagetable[i2];
    if (*pte2 & PTE_V || *pte2 & PTE_S) {
      uint64 pa2 = PTE2PA(*pte2);
      uint64_t va2 = i2 << PXSHIFT(2);
      uint64 blockno2 = PTE2BLOCKNO(*pte2);
      if (*pte2 & PTE_S) {
        printf("+-- %d: pte=%p va=%p blockno=%p", i2, pte2, va2, blockno2);
      }
      else {
        printf("+-- %d: pte=%p va=%p pa=%p", i2, pte2, va2, pa2);
      }
      if (*pte2 & PTE_V) printf(" V");
      if (*pte2 & PTE_R) printf(" R");
      if (*pte2 & PTE_W) printf(" W");
      if (*pte2 & PTE_X) printf(" X");
      if (*pte2 & PTE_U) printf(" U");
      if (*pte2 & PTE_S) printf(" S");
      // if (*pte2 & PTE_D) printf(" D");
      if (*pte2 & PTE_D && !(*pte2 & PTE_S)) printf(" D");
      if (*pte2 & PTE_P) printf(" P");
      printf("\n");

      if (!(*pte2 & PTE_R || *pte2 & PTE_W || *pte2 & PTE_X)) {
        /* level 1 */
        pagetable_t child_pagetable1 = PTE2PA(*pte2);
        
        int last1 = -1;
        for (int i1 = 0; i1 < 512; i1++) {
          if (child_pagetable1[i1] & PTE_V) {
            last1 = i1;
          }
        }

        for (uint64_t i1 = 0; i1 < 512; i1++) {
          pte_t *pte1 = &child_pagetable1[i1];

          if (*pte1 & PTE_V || *pte2 & PTE_S) {
            if (i2 == last2) printf("    ");
            else printf("|   ");

            uint64 pa1 = PTE2PA(*pte1);
            uint64_t va1 = va2 + (i1 << 21);
            uint64 blockno1 = PTE2BLOCKNO(*pte1);
            if (*pte1 & PTE_S) {
              printf("+-- %d: pte=%p va=%p blockno=%p", i1, pte1, va1, blockno1);
            }
            else {
              printf("+-- %d: pte=%p va=%p pa=%p", i1, pte1, va1, pa1);
            }
            if (*pte1 & PTE_V) printf(" V");
            if (*pte1 & PTE_R) printf(" R");
            if (*pte1 & PTE_W) printf(" W");
            if (*pte1 & PTE_X) printf(" X");
            if (*pte1 & PTE_U) printf(" U");
            if (*pte1 & PTE_S) printf(" S");
            // if (*pte1 & PTE_D) printf(" D");
            if (*pte1 & PTE_D && !(*pte1 & PTE_S)) printf(" D");
            if (*pte1 & PTE_P) printf(" P");
            printf("\n");

            if (!(*pte1 & PTE_R || *pte1 & PTE_W || *pte1 & PTE_X)) {
              /* level 0 */
              pagetable_t child_pagetable0 = PTE2PA(*pte1);

              int last0 = -1;
              for (int i0 = 0; i0 < 512; i0++) {
                if (child_pagetable0[i0] & PTE_V) {
                  last0 = i0;
                }
              }

              for (uint64_t i0 = 0; i0 < 512; i0++) {
                pte_t *pte0 = &child_pagetable0[i0];
                if (*pte0 & PTE_V || *pte0 & PTE_S) {
                  if (i2 == last2) printf("    ");
                  else printf("|   ");
                  if (i1 == last1) printf("    ");
                  else printf("|   ");

                  uint64 pa0 = PTE2PA(*pte0);
                  uint64 va0 = va1 + (i0 << 12);
                  uint64 blockno0 = PTE2BLOCKNO(*pte0);
                  // printf("i = %d\n", i0);
                  if (*pte0 & PTE_S) {
                    printf("+-- %d: pte=%p va=%p blockno=%p", i0, pte0, va0, blockno0);
                  }
                  else {
                    printf("+-- %d: pte=%p va=%p pa=%p", i0, pte0, va0, pa0);
                  }
                  if (*pte0 & PTE_V) printf(" V");
                  if (*pte0 & PTE_R) printf(" R");
                  if (*pte0 & PTE_W) printf(" W");
                  if (*pte0 & PTE_X) printf(" X");
                  if (*pte0 & PTE_U) printf(" U");
                  if (*pte0 & PTE_S) printf(" S");
                  if (*pte0 & PTE_D && !(*pte0 & PTE_S)) printf(" D");
                  // if (*pte0 & PTE_D) printf(" D");
                  if (*pte0 & PTE_P) printf(" P");
                  printf("\n");
                }
              }
            }
          }
        }
      }
    }
  }
}