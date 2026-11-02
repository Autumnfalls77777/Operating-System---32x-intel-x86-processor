/* ============================================================================
 * include/memory.h  -  Kernel heap allocator
 *
 * A boundary-tag free-list allocator over a fixed physical heap.  Each block
 * begins with a header that carries a magic value so kfree() can detect
 * invalid pointers and double frees.
 * ============================================================================ */

#ifndef NOVA_MEMORY_H
#define NOVA_MEMORY_H

#include "types.h"

#define MEM_BLOCK_MAGIC 0x4E4F5642u   /* 'NOVB' */

typedef struct mem_block {
    u32              magic;    /* MEM_BLOCK_MAGIC                       */
    u32              size;     /* Usable bytes (excluding this header)  */
    u32              free;     /* 1 = available                          */
    struct mem_block *next;    /* Next block in physical order           */
} mem_block_t;

typedef struct {
    u32 total;
    u32 used;
    u32 free;
    u32 blocks;
    u32 largest_free;
    u32 start;
} mem_info_t;

void  mem_init(u32 start, u32 size);
void *kmalloc(u32 size);
void *kzalloc(u32 size);
void  kfree(void *ptr);
void  mem_info(mem_info_t *out);
int   mem_block_info(int index, u32 *addr, u32 *size, int *free);
u32   mem_used(void);
u32   mem_free(void);
u32   mem_total(void);
int   mem_block_count(void);

#endif /* NOVA_MEMORY_H */
