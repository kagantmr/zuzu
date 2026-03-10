#ifndef __KERNEL__

#include "zmalloc.h"

#include <mem.h>
#include <stdint.h>
#include <zuzu/memprot.h>
#include <zuzu.h>

typedef struct arena
{
    uintptr_t base;   // start of arena VA range
    uintptr_t brk;    // current allocation frontier
    uintptr_t mapped; // how far _memmap has been called
} arena_t;

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
static arena_t arena;
static free_node_t *free_list = NULL;

void *zmalloc(size_t size)
{
    if (!size)
        return NULL;

    // 0. initialize
    if (!arena.base)
    {
        arena.base = (uintptr_t)_memmap(NULL, ARENA_CHUNK_SIZE, VM_PROT_READ | VM_PROT_WRITE);
        if ((intptr_t)arena.base < 0)
            return NULL;
        arena.mapped = arena.base + ARENA_CHUNK_SIZE;
        arena.brk = arena.base;
    }

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
            return (void *)curr;
        }
        prev = curr;
        curr = curr->next;
    }

    // 4. if its empty bump the arena
    if (arena.brk + total_size > arena.mapped)
    {
        uintptr_t result =
            (uintptr_t)_memmap((void *)arena.mapped, ARENA_CHUNK_SIZE, VM_PROT_READ | VM_PROT_WRITE);
        if ((intptr_t)result < 0)
            return NULL;
        arena.mapped += ARENA_CHUNK_SIZE;
    }

    uintptr_t old_brk = arena.brk;
    block_header_t *brk = (block_header_t *)arena.brk;
    *brk = (block_header_t){.size = total_size, ._pad = 0};
    arena.brk += total_size;
    return (void *)(old_brk + HEADER_SIZE);
}

void *zcalloc(size_t count, size_t size)
{
    void *mem = zmalloc(count * size);
    if (!mem)
        return NULL;
    memset(mem, 0, size * count);
    return mem;
}

void *zrealloc(void *ptr, size_t size)
{
    if (!ptr)
        return zmalloc(size);
    if (size == 0)
    {
        zfree(ptr);
        return NULL;
    }

    void *new_mem = zmalloc(size);
    if (!new_mem)
        return ptr;

    size_t old_size = ((block_header_t *)((char *)ptr - HEADER_SIZE))->size - HEADER_SIZE;
    size_t min_size = old_size < size ? old_size : size;
    memcpy(new_mem, ptr, min_size);
    zfree(ptr);
    return new_mem;
}

void zfree(void *ptr)
{
    if (!ptr)
        return;
    free_node_t *node = (free_node_t *)ptr;
    node->next = free_list;
    free_list = node;
}

#endif /* !__KERNEL__ */
