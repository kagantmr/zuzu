#ifndef KERNEL_MM_ALLOC_H
#define KERNEL_MM_ALLOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HEAP_INITIAL_SIZE (64 * 4096) // 256KB initial heap
#define HEAP_GROW_MIN_PAGES 16        // 64KB minimum grow chunk
#define ALIGNMENT 16
#define MIN_PAYLOAD ALIGNMENT

// Helper for compile-time alignment
#define ALIGN_UP_CONST(x, a) (((x) + (a) - 1) & ~((a) - 1u))

typedef struct MemBlock
{
    size_t size; // Size of the block, excluding this header
    struct MemBlock *next;
    struct MemBlock *prev;
    uint32_t state; // KBLOCK_ALLOCATED / KBLOCK_FREE magic
} KMemBlock;

#define KBLOCK_ALLOCATED 0xA110C8EDu
#define KBLOCK_FREE 0xF9EEB10Cu

typedef struct Slab
{
    struct Slab *next;             // next slab in this cache's list
    struct SlabCache *owner_cache; // owning cache for free-time validation
    size_t used;                   // how many objects are currently allocated
    size_t capacity;               // total slots in this slab
    void *free_head;               // freelist of available slots
} KHeapSlab;

typedef struct SlabCache
{
    const char *name; // "Port", for debugging/kheap_dump
    size_t obj_size;  // aligned object size
    KHeapSlab *slabs;    // linked list of all slabs
} KHeapSlabCache;

// Aligned header size used for all layout calculations
#define HDR ALIGN_UP_CONST(sizeof(KMemBlock), ALIGNMENT)

extern KMemBlock *heap_head;

/**
 * @brief Allocate uninitialized memory from the kernel heap.
 *
 * Reserved for large, variable-size buffers that the caller fills
 * immediately and completely (page-address arrays, image staging, etc).
 * For small fixed-size objects use a slab cache if one exists, otherwise
 * use kzalloc/kcalloc to prevent uninitialized memory errors.
 *
 * @param size The size of memory to allocate in bytes.
 * @return Pointer to the block, or NULL on failure.
 */
void *KMalloc(size_t size);

/**
 * @brief Allocate zero-filled memory for a single object.
 * @param size Object size in bytes.
 * @return Pointer to the zeroed block, or NULL on failure.
 */
void *KZAlloc(size_t size);

/**
 * @brief Allocate a zero-filled array with overflow-checked sizing.
 * @param nmemb Element count.
 * @param size  Element size in bytes.
 * @return Pointer to the zeroed array, or NULL on failure or size overflow.
 */
void *KCalloc(size_t nmemb, size_t size);

/**
 * @brief Free a previously allocated block of memory.
 *
 * @param ptr Pointer to the memory block to free.
 */
void KFree(void *ptr);

/**
 * @brief Initialize the kernel heap.
 */
void KHeapInit(void);

/**
 * @brief Dump the kernel heap state for debugging.
 *
 * Prints information about all blocks in the heap including addresses,
 * sizes, free status, and statistics.
 */
void KHeapDump(void);

/* Hot-path object allocators backed by slab caches. */

/**
* @brief Allocate space for a Port object.
*
* @retval NULL Out of memory
* @return void*
*/
void *KAllocPortObj(void);

/**
* @brief Free space belonging to a Port object.
*
* @param[in] Object to free.
*/
void KFreePortObj(void *ptr);

/**
* @brief Allocate space for a ReplyCap object.
*
* @retval NULL Out of memory
* @return void*
*/
void *KAllocReplyCap(void);

/**
* @brief Free space belonging to a ReplyCap object.
*
* @param[in] Object to free.
*/
void KFreeReplyCap(void *ptr);

/**
* @brief Allocate space for a DeviceCap object.
*
* @retval NULL Out of memory
* @return void*
*/
void *KAllocDevCap(void);

/**
* @brief Free space belonging to a DeviceCap object.
*
* @param[in] Object to free.
*/
void KFreeDevCap(void *ptr);

/* Generic slab-cache API for per-subsystem fixed-size object pools.
 * Declare a `static KHeapSlabCache` in the owning TU, KSlabInit it once,
 * then KSlabAlloc / KSlabFree. KSlabFree tolerates NULL. */
void KSlabInit(KHeapSlabCache *cache, const char *name, size_t obj_size);
void *KSlabAlloc(KHeapSlabCache *cache);
void KSlabFree(KHeapSlabCache *cache, void *ptr);

#endif // KERNEL_MM_ALLOC_H