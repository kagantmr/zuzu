#ifndef __KERNEL__

#include "malloc.h"

#include <mem.h>
#include <stdint.h>
#include <zuzu/memprot.h>
#include <zuzu/types.h>
#include <zuzu/zuzu.h>
#include <sbrk.h>

typedef struct
{
    size_t size;
    size_t _pad; // keep 8-byte alignment
} block_header_t;

typedef struct free_node
{
    struct free_node *next;
} free_node_t;

#define HEADER_SIZE sizeof(block_header_t)
#define ARENA_CHUNK_SIZE (64 * 1024)

static uintptr_t brk   = 0;   /* frontier within the current sbrk block */
static uintptr_t limit = 0;   /* end of that block */
static free_node_t *free_list = NULL;

void *malloc(size_t size)
{
    if (!size)
        return NULL;


    // 1. align
    size_t allocated_size = align_up(size, 8);
    // 2. add room for header
    size_t total_size = HEADER_SIZE + allocated_size;

    // 3. free list check
    free_node_t *prev = NULL;
    free_node_t *curr = free_list;
    while (curr)
    {
        block_header_t *block = (block_header_t *)((char *)curr - HEADER_SIZE);
        if (block->size >= total_size)
        {
            if (prev)
                prev->next = curr->next;
            else
                free_list = curr->next;

            if (block->size >= total_size + HEADER_SIZE + 8) {
                size_t old_size = block->size;
                block->size = total_size;
                block_header_t *block2 = (block_header_t *)((char *)curr + total_size - HEADER_SIZE);
                block2->size = old_size - total_size;
                free((char *)block2 + HEADER_SIZE);
            } 
            return (void *)curr;
        }
        prev = curr;
        curr = curr->next;
    }

    if (brk + total_size > limit)
        {
            size_t want = total_size > ARENA_CHUNK_SIZE ? total_size : ARENA_CHUNK_SIZE;
            void *chunk = sbrk((intptr_t)want);
            if (chunk == (void *)-1) return NULL;

            /* Donate the tail of the previous block to the free list rather than
            * abandoning it. sbrk is contiguous, so the new block starts exactly
            * at the old limit — but don't rely on that; just bank the leftover. */
            size_t tail = limit - brk;
            if (tail >= HEADER_SIZE + 8) {
                block_header_t *h = (block_header_t *)brk;
                h->size = tail;
                h->_pad = 0;
                free((char *)h + HEADER_SIZE);
            }

            brk   = (uintptr_t)chunk;
            limit = brk + want;
        }

        /* 3. bump */
        block_header_t *h = (block_header_t *)brk;
        h->size = total_size;
        h->_pad = 0;
        uintptr_t old = brk;
        brk += total_size;
        return (void *)(old + HEADER_SIZE);
}

void *calloc(size_t count, size_t size)
{
    if (count != 0 && size > ((size_t)-1) / count)
        return NULL;

    size_t total = count * size;
    void *mem = malloc(total);
    if (!mem)
        return NULL;
    memset(mem, 0, total);
    return mem;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);
    if (size == 0)
    {
        free(ptr);
        return NULL;
    }

    void *new_mem = malloc(size);
    if (!new_mem)
        return ptr;

    size_t old_size = ((block_header_t *)((char *)ptr - HEADER_SIZE))->size - HEADER_SIZE;
    size_t min_size = old_size < size ? old_size : size;
    memcpy(new_mem, ptr, min_size);
    free(ptr);
    return new_mem;
}

void free(void *ptr)
{
    if (!ptr)
        return;

    block_header_t *freed = (block_header_t *)((char *)ptr - HEADER_SIZE);
    free_node_t *node = (free_node_t *)ptr;

    // Sorted insert by address
    free_node_t *prev = NULL;
    free_node_t *curr = free_list;
    while (curr && (uintptr_t)curr < (uintptr_t)node)
    {
        prev = curr;
        curr = curr->next;
    }

    // Insert between prev and curr
    node->next = curr;
    if (prev)
        prev->next = node;
    else
        free_list = node;

    // Coalesce with next neighbor
    if (curr)
    {
        block_header_t *next_hdr = (block_header_t *)((char *)curr - HEADER_SIZE);
        if ((char *)freed + freed->size == (char *)next_hdr)
        {
            freed->size += next_hdr->size;
            node->next = curr->next;
        }
    }

    // Coalesce with previous neighbor
    if (prev)
    {
        block_header_t *prev_hdr = (block_header_t *)((char *)prev - HEADER_SIZE);
        if ((char *)prev_hdr + prev_hdr->size == (char *)freed)
        {
            prev_hdr->size += freed->size;
            prev->next = node->next;
        }
    }
}

#endif /* !__KERNEL__ */