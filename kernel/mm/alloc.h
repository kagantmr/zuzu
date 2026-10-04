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

typedef struct KHeapBlockStruct {
    size_t size; // Size of the block, excluding this header
    struct KHeapBlockStruct *next;
    struct KHeapBlockStruct *prev;
    uint32_t state; // KBLOCK_ALLOCATED / KBLOCK_FREE magic
} KHeapBlock;

#define KBLOCK_ALLOCATED 0xA110C8EDu
#define KBLOCK_FREE 0xF9EEB10Cu

typedef struct KSlabStruct {
    struct KSlabStruct *next, *prev;      // intrusive: links within one of the cache's lists
    struct KSlabCacheStruct *owner_cache; // owning cache for free-time validation
    size_t used;                          // how many objects are currently allocated
    size_t capacity;                      // total slots in this slab
    void *free_head;                      // freelist of available slots
} KSlab;

typedef struct KSlabCacheStruct {
    size_t obj_size;   // aligned object size
    KSlab *partial;    // slabs with >= 1 free slot
    KSlab *full;       // slabs with 0 free slots
    KSlab *empty_hold; // at most one all-free slab, kept as grow hysteresis
} KSlabCache;

// Aligned header size used for all layout calculations
#define HDR ALIGN_UP_CONST(sizeof(KHeapBlock), ALIGNMENT)

extern KHeapBlock *heap_head;

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

/* Generic slab-cache API for per-subsystem fixed-size object pools.
 * Declare a `static KSlabCache` in the owning TU, KSlabInit it once,
 * then KSlabAlloc / KSlabFree. KSlabFree tolerates NULL. */
void KSlabInit(KSlabCache *cache, size_t obj_size);
void *KSlabAlloc(KSlabCache *cache);
void KSlabFree(KSlabCache *cache, void *ptr);

#endif // KERNEL_MM_ALLOC_H
