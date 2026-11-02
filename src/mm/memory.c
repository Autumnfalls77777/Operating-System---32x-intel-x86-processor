/* ============================================================================
 * src/mm/memory.c  -  Kernel heap allocator (first-fit free list)
 * ============================================================================ */

#include "kernel.h"
#include "memory.h"

static mem_block_t *head;
static u32 heap_start;
static u32 heap_size;

static inline int block_valid(mem_block_t *b)
{
    return b && b->magic == MEM_BLOCK_MAGIC;
}

void mem_init(u32 start, u32 size)
{
    heap_start = start;
    heap_size = size;

    head = (mem_block_t *)start;
    head->magic = MEM_BLOCK_MAGIC;
    head->size = size - (u32)sizeof(mem_block_t);
    head->free = 1;
    head->next = NULL;
}

void *kmalloc(u32 size)
{
    if (size == 0 || !head) return NULL;
    size = (size + 7u) & ~7u;

    for (mem_block_t *block = head; block; block = block->next) {
        if (!block->free || block->size < size) continue;

        if (block->size >= size + (u32)sizeof(mem_block_t) + 16u) {
            mem_block_t *remainder = (mem_block_t *)
                ((u8 *)block + sizeof(mem_block_t) + size);
            remainder->magic = MEM_BLOCK_MAGIC;
            remainder->size = block->size - size - (u32)sizeof(mem_block_t);
            remainder->free = 1;
            remainder->next = block->next;
            block->size = size;
            block->next = remainder;
        }

        block->free = 0;
        return (u8 *)block + sizeof(mem_block_t);
    }

    klog(KLOG_LEVEL_WARN, "kmalloc(%u) failed - heap exhausted", size);
    return NULL;
}

void *kzalloc(u32 size)
{
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void kfree(void *ptr)
{
    if (!ptr) return;
    if ((u32)ptr < heap_start || (u32)ptr >= heap_start + heap_size) {
        klog(KLOG_LEVEL_ERR, "kfree: pointer outside heap: %p", ptr);
        return;
    }

    mem_block_t *block = (mem_block_t *)((u8 *)ptr - sizeof(mem_block_t));
    if (!block_valid(block)) {
        klog(KLOG_LEVEL_ERR, "kfree: invalid block magic at %p", block);
        return;
    }
    if (block->free) {
        klog(KLOG_LEVEL_WARN, "kfree: double free at %p", ptr);
        return;
    }

    block->free = 1;

    /* Coalesce with the following block. */
    if (block->next && block_valid(block->next) && block->next->free) {
        block->size += (u32)sizeof(mem_block_t) + block->next->size;
        block->next = block->next->next;
    }
}

void mem_info(mem_info_t *out)
{
    if (!out) return;
    out->total = heap_size;
    out->used = 0;
    out->free = 0;
    out->blocks = 0;
    out->largest_free = 0;
    out->start = heap_start;

    for (mem_block_t *b = head; b; b = b->next) {
        out->blocks++;
        if (b->free) {
            out->free += b->size;
            if (b->size > out->largest_free) out->largest_free = b->size;
        } else {
            out->used += b->size;
        }
    }
}

int mem_block_info(int index, u32 *addr, u32 *size, int *free_flag)
{
    int i = 0;
    for (mem_block_t *b = head; b; b = b->next, i++) {
        if (i == index) {
            if (addr) *addr = (u32)b;
            if (size) *size = b->size;
            if (free_flag) *free_flag = b->free;
            return 1;
        }
    }
    return 0;
}

u32 mem_used(void)
{
    mem_info_t info;
    mem_info(&info);
    return info.used;
}

u32 mem_free(void)
{
    mem_info_t info;
    mem_info(&info);
    return info.free;
}

u32 mem_total(void) { return heap_size; }

int mem_block_count(void)
{
    mem_info_t info;
    mem_info(&info);
    return (int)info.blocks;
}
