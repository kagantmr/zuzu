#include "alloc.h"
#include "pmm.h"
#include "kernel/layout.h"
#include "kernel/mm/vmm.h" 
#include "stdbool.h"
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "kernel/dev/devcap.h"
#include "core/panic.h"
#include "kernel/ipc/port.h"
#include <compiler.h>

#define LOG_FMT(fmt) "(mm) " fmt
#include <zuzu/log.h>

extern kernel_layout_t kernel_layout;

#ifdef ZUZU_BENCH

#include "kernel/bench.h"

BENCH_STAT(g_bench_reply_cap_alloc, "reply-cap alloc");
BENCH_STAT(g_bench_reply_cap_free, "reply-cap free");
#endif

static KHeapSlabCache port_cache;
static KHeapSlabCache reply_cap_cache;
static KHeapSlabCache device_cap_cache;
static bool hot_caches_ready;

KMemBlock* heap_head = NULL;
static KMemBlock* heap_tail = NULL;

/* Doubly-linked so a slab can be pulled from the middle of full/partial in
 * O(1) when a free/alloc changes its fill state. */
static __always_inline void SlabListPush(KHeapSlab **head, KHeapSlab *slab)
{
    slab->prev = NULL;
    slab->next = *head;
    if (*head)
        (*head)->prev = slab;
    *head = slab;
}

static __always_inline void SlabListRemove(KHeapSlab **head, KHeapSlab *slab)
{
    if (slab->prev)
        slab->prev->next = slab->next;
    else
        *head = slab->next;
    if (slab->next)
        slab->next->prev = slab->prev;
    slab->next = slab->prev = NULL;
}

/* New slab page, all slots free, pushed onto the cache's partial list. */
static KHeapSlab *SlabGrow(KHeapSlabCache *cache)
{
    PhysAddr pa = PmmAllocFrame();
    if (!pa) return NULL;

    KHeapSlab *slab = (KHeapSlab *)PA_TO_VA(pa);
    size_t hdr_size = align_up(sizeof(KHeapSlab), 8);
    uint8_t *data = (uint8_t *)slab + hdr_size;
    size_t usable = PAGE_SIZE - hdr_size;

    slab->owner_cache = cache;
    slab->capacity = usable / cache->obj_size;
    slab->used = 0;
    slab->free_head = NULL;
    slab->state = SLAB_PARTIAL;

    // build freelist: chain all slots together
    for (size_t i = 0; i < slab->capacity; i++) {
        void *slot = data + (i * cache->obj_size);
        *(void **)slot = slab->free_head;
        slab->free_head = slot;
    }

    SlabListPush(&cache->partial, slab);
    return slab;
}

static void CreateSlabCache(KHeapSlabCache *cache, const char *name, size_t obj_size)
{
    // enforce minimum: must fit a freelist pointer
    if (obj_size < sizeof(void *))
        obj_size = sizeof(void *);
    // align up to 8 for ARM alignment
    cache->obj_size = align_up(obj_size, 8);
    cache->name = name;
    cache->partial = NULL;
    cache->full = NULL;
    cache->empty_hold = NULL;
    // at least one object must fit in a slab page after the header
    assert(cache->obj_size <= PAGE_SIZE - align_up(sizeof(KHeapSlab), 8));
}

static void *__hot SlabAlloc(KHeapSlabCache *cache)
{
    KHeapSlab *slab = cache->partial;

    if (unlikely(!slab)) {
        // Reuse the held empty slab before touching the PMM.
        if (cache->empty_hold) {
            slab = cache->empty_hold;
            cache->empty_hold = NULL;
            slab->state = SLAB_PARTIAL;
            SlabListPush(&cache->partial, slab);
        } else {
            slab = SlabGrow(cache);
            if (unlikely(!slab)) return NULL;
        }
    }

    // pop from freelist
    void *obj = slab->free_head;
    slab->free_head = *(void **)obj;
    slab->used++;

    if (unlikely(!slab->free_head)) {
        // slab is now full: move partial -> full
        SlabListRemove(&cache->partial, slab);
        SlabListPush(&cache->full, slab);
        slab->state = SLAB_FULL;
    }
    return obj;
}

static __always_inline void SlabFree(KHeapSlabCache *cache, void *ptr)
{
    assert(cache != NULL);
    assert(ptr != NULL);

    // the slab header is at the page-aligned base of this pointer
    KHeapSlab *slab = (KHeapSlab *)align_down((uintptr_t)ptr, PAGE_SIZE);
    assert(slab->owner_cache == cache);

    bool was_full = (slab->free_head == NULL);

    // push onto freelist
    *(void **)ptr = slab->free_head;
    slab->free_head = ptr;
    slab->used--;

    if (unlikely(was_full)) {
        SlabListRemove(&cache->full, slab);
        SlabListPush(&cache->partial, slab);
        slab->state = SLAB_PARTIAL;
    }

    if (unlikely(slab->used == 0)) {
        SlabListRemove(&cache->partial, slab);
        if (cache->empty_hold == NULL) {
            cache->empty_hold = slab;
            slab->state = SLAB_EMPTY;
        } else {
            PmmFreeFrame(VA_TO_PA((uintptr_t)slab));
        }
    }
}

static __always_inline void SlabCachesInit(void)
{
    if (likely(hot_caches_ready))
        return;

    CreateSlabCache(&port_cache, "Port", sizeof(Port));
    CreateSlabCache(&reply_cap_cache, "ReplyCap", sizeof(ReplyCap));
    CreateSlabCache(&device_cap_cache, "DeviceCap", sizeof(DeviceCap));
    hot_caches_ready = true;
}

/* Generic slab-cache API for subsystems that want a dedicated fixed-size
 * object pool (see kalloc/kfree helpers below for the IPC hot-path ones).
 * A cache is lazily usable: KSlabAlloc on a zeroed cache initializes it. */
void KSlabInit(KHeapSlabCache *cache, const char *name, size_t obj_size)
{
    CreateSlabCache(cache, name, obj_size);
}

void *KSlabAlloc(KHeapSlabCache *cache) { return SlabAlloc(cache); }

void KSlabFree(KHeapSlabCache *cache, void *ptr)
{
    if (ptr)
        SlabFree(cache, ptr);
}

static void HeapAppendBlk(KMemBlock *block)
{
    block->next = NULL;
    block->prev = heap_tail;
    if (!heap_head) {
        heap_head = block;
        heap_tail = block;
        return;
    }
    heap_tail->next = block;
    heap_tail = block;
}

static bool HeapGrow(size_t min_payload)
{
    size_t wanted = align_up(min_payload + HDR + MIN_PAYLOAD, PAGE_SIZE);
    size_t pages = wanted / PAGE_SIZE;
    if (pages < HEAP_GROW_MIN_PAGES) {
        pages = HEAP_GROW_MIN_PAGES;
    }

    PhysAddr heap_pa = PmmAllocFramesContig(pages);
    if (!heap_pa) {
        return false;
    }

    VirtAddr heap_va = PA_TO_VA(heap_pa);
    KMemBlock *block = (KMemBlock *)heap_va;
    block->size = align_down(pages * PAGE_SIZE - HDR, ALIGNMENT);
    block->state = KBLOCK_FREE;
    block->next = NULL;
    block->prev = NULL;

    HeapAppendBlk(block);

    PhysAddr seg_end_pa = heap_pa + (pages * PAGE_SIZE);
    VirtAddr seg_end_va = heap_va + (pages * PAGE_SIZE);

    if (kernel_layout.heap_start_pa == 0 || heap_pa < kernel_layout.heap_start_pa) {
        kernel_layout.heap_start_pa = heap_pa;
        kernel_layout.heap_start_va = (void *)heap_va;
    }
    if (seg_end_pa > kernel_layout.heap_end_pa) {
        kernel_layout.heap_end_pa = seg_end_pa;
        kernel_layout.heap_end_va = (void *)seg_end_va;
    }

    KDEBUG("Heap growth: heap size is now %u KB", wanted / 1024);
    return true;
}

/* Absorb block->next into block when the two are physically adjacent and
 * next is free. Maintains prev links and heap_tail. */
static void HeapMerge(KMemBlock *block)
{
    KMemBlock *next = block->next;
    if (!next || next->state != KBLOCK_FREE)
        return;
    if ((uint8_t *)block + HDR + block->size != (uint8_t *)next)
        return;

    block->size += HDR + next->size;
    block->next = next->next;
    if (next->next)
        next->next->prev = block;
    else
        heap_tail = block;
}

void* KMalloc(size_t size) {
    if (!size) {
        return NULL;
    }
    size_t req = align_up(size, ALIGNMENT); // align area up

    for (int pass = 0; pass < 2; pass++) {
        for (KMemBlock *current_block = heap_head; current_block;
             current_block = current_block->next) {
            if (current_block->state != KBLOCK_FREE || current_block->size < req)
                continue;

            size_t leftover = current_block->size - req;
            if (leftover >= HDR + MIN_PAYLOAD + (ALIGNMENT - 1)) { // split
                KMemBlock *new_block =
                    (KMemBlock *)(void *)((uint8_t *)current_block + HDR + req);

                new_block->size = align_down(leftover - HDR, ALIGNMENT);
                new_block->state = KBLOCK_FREE;
                new_block->next = current_block->next;
                new_block->prev = current_block;
                if (current_block->next)
                    current_block->next->prev = new_block;
                else
                    heap_tail = new_block;
                current_block->next = new_block;
                current_block->size = req;
            }

            current_block->state = KBLOCK_ALLOCATED;
            return (void *)((uint8_t *)current_block + HDR);
        }

        if (!HeapGrow(req)) {
            break;
        }
    }

    KERROR("No space in kernel heap");
    return NULL;
}

void *KZAlloc(size_t size)
{
    void *p = KMalloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void *KCalloc(size_t nmemb, size_t size)
{
    if (nmemb && size > (size_t)-1 / nmemb) {
        KERROR("kcalloc: size overflow (%u x %u)", (unsigned)nmemb, (unsigned)size);
        return NULL;
    }
    return KZAlloc(nmemb * size);
}


void KFree(void* ptr) {
    if (!ptr) {
        return;
    }

    // Sanity check: ptr must be aligned
    if (((uintptr_t)ptr % ALIGNMENT) != 0) {
        KERROR("kfree: pointer not aligned");
        return;
    }

    if ((uint8_t *)ptr < (uint8_t *)kernel_layout.heap_start_va + HDR ||
        (uint8_t *)ptr >= (uint8_t *)kernel_layout.heap_end_va) {
        KERROR("kfree: pointer outside kernel heap");
        return;
    }

    KMemBlock *header = (KMemBlock *)(void *)((uint8_t *)ptr - HDR);

    if (header->state == KBLOCK_FREE) {
        KERROR("Double free in kernel heap");
        return;
    }
    if (header->state != KBLOCK_ALLOCATED) {
        KERROR("kfree: pointer does not match any allocated heap block");
        return;
    }
    header->state = KBLOCK_FREE;

    // Forward merge: absorb the next block if adjacent and free.
    HeapMerge(header);

    // Backward merge: O(1) via prev pointer. Absorbs header (and whatever it
    // just merged) into prev.
    if (header->prev && header->prev->state == KBLOCK_FREE)
        HeapMerge(header->prev);
}

_Static_assert(HEAP_INITIAL_SIZE % PAGE_SIZE == 0, "Heap is not aligned to page");

void KHeapInit(void) {
    heap_head = NULL;
    heap_tail = NULL;
    kernel_layout.heap_start_pa = 0;
    kernel_layout.heap_end_pa = 0;
    kernel_layout.heap_start_va = NULL;
    kernel_layout.heap_end_va = NULL;

    if (!HeapGrow(HEAP_INITIAL_SIZE - HDR)) {
        panic("Heap could not be allocated");
    }

    SlabCachesInit();
}

void *KAllocPortObj(void)
{
    SlabCachesInit();
    return SlabAlloc(&port_cache);
}

void KFreePortObj(void *ptr)
{
    if (!ptr)
        return;
    SlabFree(&port_cache, ptr);
}

void *__hot KAllocReplyCap(void)
{
#ifdef ZUZU_BENCH
    uint32_t bench_start = BENCH_BEGIN();
#endif
    SlabCachesInit();
    void *ptr = SlabAlloc(&reply_cap_cache);
#ifdef ZUZU_BENCH
    BENCH_END(g_bench_reply_cap_alloc, bench_start);
#endif
    return ptr;
}

void __hot KFreeReplyCap(void *ptr)
{
#ifdef ZUZU_BENCH
    uint32_t bench_start = BENCH_BEGIN();
#endif
    if (unlikely(!ptr))
        return;
    SlabFree(&reply_cap_cache, ptr);
#ifdef ZUZU_BENCH
    BENCH_END(g_bench_reply_cap_free, bench_start);
#endif
}

void *KAllocDevCap(void)
{
    SlabCachesInit();
    return SlabAlloc(&device_cap_cache);
}

void KFreeDevCap(void *ptr)
{
    if (!ptr)
        return;
    SlabFree(&device_cap_cache, ptr);
}

void KHeapDump(void) {
    KINFO("*** HEAP DUMP ***");
    // Print both PA and VA for clarity
    KINFO("Heap: %p - %p", kernel_layout.heap_start_va, kernel_layout.heap_end_va);
    
    KMemBlock* current = heap_head;
    int block_num = 0;
    size_t total_free = 0;
    size_t total_used = 0;
    
    while (current) {
        // Sanity check: ensure current is within heap bounds (use _va)
        if ((uint8_t*)current < (uint8_t*)kernel_layout.heap_start_va ||
            (uint8_t*)current >= (uint8_t*)kernel_layout.heap_end_va) {
            KERROR("Block %d corrupted - pointer %p outside heap bounds", block_num, current);
            break;
        }
        
        bool is_free = (current->state == KBLOCK_FREE);
        KINFO("Block %d: addr=%p size=%u free=%d next=%p",
              block_num, current, current->size, is_free, current->next);

        if (is_free) {
            total_free += current->size;
        } else {
            total_used += current->size;
        }
        
        current = current->next;
        block_num++;
        
        // Prevent infinite loop in case of corruption
        if (block_num > 1000) {
            KERROR("Too many blocks - possible corruption");
            break;
        }
    }
    
    KINFO("Total blocks: %d, Free: %u bytes, Used: %u bytes", 
          block_num, total_free, total_used);
}
