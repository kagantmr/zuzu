/**
 * @file alloc.h
 * @brief Kernel heap (first-fit, coalescing) and fixed-size slab caches.
 */

#ifndef KERNEL_MM_ALLOC_H
#define KERNEL_MM_ALLOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Size of the first heap segment, in bytes; a multiple of the page size. */
#define HEAP_INITIAL_SIZE (64 * 4096)
/** @brief Fewest pages the heap grows by at once. */
#define HEAP_GROW_MIN_PAGES 16
/** @brief Alignment of every heap payload, in bytes. */
#define ALIGNMENT 16
/** @brief Smallest payload a heap block can hold. */
#define MIN_PAYLOAD ALIGNMENT

/** @brief Rounds @p x up to a multiple of the power of two @p a, as a constant expression. */
#define ALIGN_UP_CONST(x, a) (((x) + (a) - 1) & ~((a) - 1u))

/**
 * @brief Header in front of every heap block.
 */
typedef struct KHeapBlockStruct {
    size_t size;                    /**< Payload size in bytes, excluding this header. */
    struct KHeapBlockStruct *next;  /**< Next block in address order, or NULL. */
    struct KHeapBlockStruct *prev;  /**< Previous block in address order, or NULL. */
    uint32_t state;                 /**< KBLOCK_ALLOCATED or KBLOCK_FREE. */
} KHeapBlock;

/** @brief KHeapBlock::state of a block in use. */
#define KBLOCK_ALLOCATED 0xA110C8EDu
/** @brief KHeapBlock::state of a free block. */
#define KBLOCK_FREE 0xF9EEB10Cu

/**
 * @brief One page of equally sized objects; this header sits at the start of the page.
 */
typedef struct KSlabStruct {
    struct KSlabStruct *next;               /**< Next slab in the cache list. */
    struct KSlabStruct *prev;               /**< Previous slab in the cache list. */
    struct KSlabCacheStruct *owner_cache;   /**< Cache the slab belongs to. */
    size_t used;                            /**< Objects currently allocated. */
    size_t capacity;                        /**< Objects the slab holds. */
    void *free_head;                        /**< First free object; each free object stores the next. */
} KSlab;

/**
 * @brief A cache of fixed-size objects backed by slabs.
 */
typedef struct KSlabCacheStruct {
    size_t obj_size;    /**< Object size in bytes, rounded up to 8. */
    KSlab *partial;     /**< Slabs with free and used objects. */
    KSlab *full;        /**< Slabs with no free object. */
    KSlab *empty_hold;  /**< One fully free slab kept back to avoid page churn, or NULL. */
} KSlabCache;

/** @brief Size of a KHeapBlock header, padded to ALIGNMENT. */
#define HDR ALIGN_UP_CONST(sizeof(KHeapBlock), ALIGNMENT)

/** @brief First heap block in address order, or NULL before KHeapInit(). */
extern KHeapBlock *heap_head;

/**
 * @brief Allocates kernel heap memory, growing the heap when no block fits.
 *
 * @param[in] size Bytes wanted, rounded up to ALIGNMENT.
 * @return A 16-byte aligned block, or NULL if @p size is 0 or memory ran out.
 */
void *KMalloc(size_t size);

/**
 * @brief Allocates zero-filled kernel heap memory.
 *
 * @param[in] size Bytes wanted.
 * @return As KMalloc(), with the block zeroed.
 */
void *KZAlloc(size_t size);

/**
 * @brief Allocates a zero-filled array.
 *
 * @param[in] nmemb Number of elements.
 * @param[in] size  Size of each element.
 * @return As KZAlloc(), or NULL if @p nmemb x @p size overflows.
 */
void *KCalloc(size_t nmemb, size_t size);

/**
 * @brief Returns a block to the heap and merges it with free neighbours.
 *
 * Logs and ignores misuse: a misaligned pointer, one outside the heap, a double
 * free or a pointer that is not a block.
 *
 * @param[in] ptr Block from KMalloc(), or NULL (ignored).
 */
void KFree(void *ptr);

/**
 * @brief Sets up the kernel heap with its first segment; panics if memory is unavailable.
 */
void KHeapInit(void);

/**
 * @brief Initialises a slab cache.
 *
 * @param[out] cache    Cache to set up.
 * @param[in]  obj_size Object size; raised to a pointer and rounded up to 8. Must fit in a page
 *                      after the slab header.
 */
void KSlabInit(KSlabCache *cache, size_t obj_size);
/**
 * @brief Takes an object from a slab cache, adding a page when none is free.
 *
 * @param[in,out] cache The cache.
 * @return The object, uninitialised, or NULL if no page could be allocated.
 */
void *KSlabAlloc(KSlabCache *cache);
/**
 * @brief Returns an object to its slab cache.
 *
 * A slab that becomes empty is kept as the cache's spare, or its page is freed if one is
 * already held.
 *
 * @param[in,out] cache The cache the object came from.
 * @param[in]     ptr   The object, or NULL (ignored).
 */
void KSlabFree(KSlabCache *cache, void *ptr);

#endif // KERNEL_MM_ALLOC_H
