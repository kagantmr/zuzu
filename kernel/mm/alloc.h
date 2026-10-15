#ifndef KERNEL_MM_ALLOC_H
#define KERNEL_MM_ALLOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HEAP_INITIAL_SIZE (64 * 4096)
#define HEAP_GROW_MIN_PAGES 16
#define ALIGNMENT 16
#define MIN_PAYLOAD ALIGNMENT

#define ALIGN_UP_CONST(x, a) (((x) + (a) - 1) & ~((a) - 1u))

typedef struct KHeapBlockStruct {
    size_t size;
    struct KHeapBlockStruct *next;
    struct KHeapBlockStruct *prev;
    uint32_t state;
} KHeapBlock;

#define KBLOCK_ALLOCATED 0xA110C8EDu
#define KBLOCK_FREE 0xF9EEB10Cu

typedef struct KSlabStruct {
    struct KSlabStruct *next, *prev;
    struct KSlabCacheStruct *owner_cache;
    size_t used;
    size_t capacity;
    void *free_head;
} KSlab;

typedef struct KSlabCacheStruct {
    size_t obj_size;
    KSlab *partial;
    KSlab *full;
    KSlab *empty_hold;
} KSlabCache;

#define HDR ALIGN_UP_CONST(sizeof(KHeapBlock), ALIGNMENT)

extern KHeapBlock *heap_head;

void *KMalloc(size_t size);

void *KZAlloc(size_t size);

void *KCalloc(size_t nmemb, size_t size);

void KFree(void *ptr);

void KHeapInit(void);

void KSlabInit(KSlabCache *cache, size_t obj_size);
void *KSlabAlloc(KSlabCache *cache);
void KSlabFree(KSlabCache *cache, void *ptr);

#endif // KERNEL_MM_ALLOC_H
