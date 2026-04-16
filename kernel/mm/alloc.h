#ifndef KERNEL_MM_ALLOC_H
#define KERNEL_MM_ALLOC_H

#include "stddef.h"
#include "stdbool.h"
#include "stdint.h"

#define HEAP_INITIAL_SIZE (512 * 4096)  // 2MB initial heap
#define HEAP_GROW_MIN_PAGES 16          // 64KB minimum grow chunk
#define ALIGNMENT 16
#define MIN_PAYLOAD ALIGNMENT

// Helper for compile-time alignment
#define ALIGN_UP_CONST(x, a) (((x) + (a) - 1) & ~((a) - 1))

typedef struct mem_block {
    size_t size;           // Size of the block, excluding this header
    struct mem_block *next;
    bool free;            
} kmem_block_t;

typedef struct slab {
    struct slab *next;       // next slab in this cache's list
    uint16_t    used;        // how many objects are currently allocated
    uint16_t    capacity;    // total slots in this slab
    void       *free_head;   // freelist of available slots
} slab_t;

typedef struct slab_cache {
    const char *name;        // "endpoint_t", for debugging/kheap_dump
    size_t      obj_size;    // aligned object size
    slab_t     *slabs;       // linked list of all slabs
} slab_cache_t;

// Aligned header size used for all layout calculations
#define HDR ALIGN_UP_CONST(sizeof(kmem_block_t), ALIGNMENT)

extern kmem_block_t* heap_head;

/**
    * @brief Allocate a block of memory of the specified size.
    * @param size The size of memory to allocate in bytes.
    * @return A pointer to the allocated memory block, or NULL if allocation fails.
 */
void* kmalloc(size_t size);

/**
 * @brief Free a previously allocated block of memory.
 * 
 * @param ptr Pointer to the memory block to free.
 */
void kfree(void* ptr);

/**
 * @brief Initialize the kernel heap.
 */
void kheap_init(void);

/**
 * @brief Dump the kernel heap state for debugging.
 * 
 * Prints information about all blocks in the heap including addresses,
 * sizes, free status, and statistics.
 */
void kheap_dump(void);

/* Hot-path object allocators backed by slab caches. */
void *kalloc_endpoint(void);
void kfree_endpoint(void *ptr);

void *kalloc_reply_cap(void);
void kfree_reply_cap(void *ptr);

void *kalloc_device_cap(void);
void kfree_device_cap(void *ptr);

#endif // KERNEL_MM_ALLOC_H