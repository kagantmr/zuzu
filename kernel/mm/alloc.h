#include "stddef.h"
#include "stdbool.h"
#include "stdint.h"

#define HEAP_SIZE (32 * 4096)  // 128KB heap
#define ALIGNMENT 16
#define MIN_PAYLOAD ALIGNMENT

// Helper for compile-time alignment
#define ALIGN_UP_CONST(x, a) (((x) + (a) - 1) & ~((a) - 1))

typedef struct mem_block {
    size_t size;           // Size of the block, excluding this header
    struct mem_block *next;
    bool free;            
} kmem_block_t;

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