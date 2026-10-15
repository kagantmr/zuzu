#include "l2_pool.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm/pmm.h"
#include "types.h"
#include <arch/mmu.h>
#include <string.h>

#define L2_TABLE_SIZE 1024U
#define L2_PER_PAGE (PAGE_SIZE / L2_TABLE_SIZE)
#define L2_SLOTS_FULL ((1u << L2_PER_PAGE) - 1u)
#define PAGE_OFFSET_MASK (PAGE_SIZE - 1u)

static L2PtPoolEntry *pool_head = NULL;
static KSlabCache l2_entry_cache;

uintptr_t L2PtPoolAlloc(void)
{
    for (L2PtPoolEntry *entry = pool_head; entry; entry = entry->next) {
        if (entry->used_mask == L2_SLOTS_FULL)
            continue;

        for (unsigned slot = 0; slot < L2_PER_PAGE; slot++) {
            if (!(entry->used_mask & (1 << slot))) {
                entry->used_mask = (uint8_t)(entry->used_mask | (1U << slot));
                uintptr_t pa = entry->page_pa + (slot * L2_TABLE_SIZE);
                memset((void *)PA_TO_VA(pa), 0, L2_TABLE_SIZE);
                return pa;
            }
        }
    }

    uintptr_t page_pa = PmmAllocFrame();
    if (!page_pa) {
        return 0;
    }

    if (!l2_entry_cache.obj_size)
        KSlabInit(&l2_entry_cache, sizeof(L2PtPoolEntry));
    L2PtPoolEntry *entry = KSlabAlloc(&l2_entry_cache);
    if (!entry) {
        PmmFreeFrame(page_pa);
        return 0;
    }

    memset((void *)PA_TO_VA(page_pa), 0, PAGE_SIZE);

    entry->page_pa = page_pa;
    entry->used_mask = 0x1;
    entry->next = pool_head;
    pool_head = entry;

    return page_pa;
}

void L2PtPoolFree(PhysAddr l2_pa)
{
    if (!l2_pa) {
        return;
    }

    uintptr_t page_pa = l2_pa & ~PAGE_OFFSET_MASK;
    int slot = (int)((l2_pa & PAGE_OFFSET_MASK) / L2_TABLE_SIZE);

    L2PtPoolEntry *prev = NULL;
    L2PtPoolEntry *entry = pool_head;

    while (entry) {
        if (entry->page_pa != page_pa) {
            prev = entry;
            entry = entry->next;
            continue;
        }

        entry->used_mask = (uint8_t)(entry->used_mask & ~(1U << slot));

        if (entry->used_mask == 0) {
            PmmFreeFrame(page_pa);
            if (prev) {
                prev->next = entry->next;
            } else {
                pool_head = entry->next;
            }
            KSlabFree(&l2_entry_cache, entry);
        }
        return;
    }
}
