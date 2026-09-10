// l2_pool.c - L2 page pool implementation for ARM MMU

#include "l2_pool.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/alloc.h"
#include "zuzu/types.h"
#include <arch/mmu.h>
#include <string.h>
#include <spinlock.h>

#define L2_TABLE_SIZE 1024U                 // one ARMv7 L2 table is 1 KB
#define L2_PER_PAGE (PAGE_SIZE / L2_TABLE_SIZE) // 4 L2 tables packed per 4 KB page
#define L2_SLOTS_FULL ((1u << L2_PER_PAGE) - 1u) // used_mask value when all slots taken
#define PAGE_OFFSET_MASK (PAGE_SIZE - 1u)

static L2PtPoolEntry *pool_head = NULL;
static KHeapSlabCache l2_entry_cache;

uintptr_t L2PtPoolAlloc(void)
{
    for (L2PtPoolEntry *entry = pool_head; entry; entry = entry->next)
    {
        if (entry->used_mask == L2_SLOTS_FULL)
            continue; // all slots occupied

        // Find the first free slot in this page
        for (unsigned slot = 0; slot < L2_PER_PAGE; slot++)
        {
            if (!(entry->used_mask & (1 << slot)))
            {
                entry->used_mask = (uint8_t)(entry->used_mask | (1U << slot));
                uintptr_t pa = entry->page_pa + (slot * L2_TABLE_SIZE);
                memset((void *)PA_TO_VA(pa), 0, L2_TABLE_SIZE);
                return pa;
            }
        }
    }

    // No existing page has free slots, need to allocate a new page
    uintptr_t page_pa = PmmAllocFrame();
    if (!page_pa)
    {
        return 0; // out of physical memory
    }

    if (!l2_entry_cache.obj_size)
        KSlabInit(&l2_entry_cache, "L2PtPoolEntry", sizeof(L2PtPoolEntry));
    L2PtPoolEntry *entry = KSlabAlloc(&l2_entry_cache);
    if (!entry)
    {
        PmmFreeFrame(page_pa);
        return 0; // out of memory for pool entry
    }

    memset((void *)PA_TO_VA(page_pa), 0, PAGE_SIZE); // zero whole page

    entry->page_pa = page_pa;
    entry->used_mask = 0x1; // slot 0 claimed
    entry->next = pool_head;
    pool_head = entry;

    return page_pa;                               // slot 0 is at offset 0
}

void L2PtPoolFree(PhysAddr l2_pa)
{
    if (!l2_pa)
    {
        return;
    }

    uintptr_t page_pa = l2_pa & ~PAGE_OFFSET_MASK;
    int slot = (int)((l2_pa & PAGE_OFFSET_MASK) / L2_TABLE_SIZE);

    L2PtPoolEntry *prev = NULL;
    L2PtPoolEntry *entry = pool_head;

    while (entry)
    {
        if (entry->page_pa != page_pa)
        {
            prev = entry;
            entry = entry->next;
            continue;
        }

        entry->used_mask = (uint8_t)(entry->used_mask & ~(1U << slot));

        // If all 4 slots free, return page to PMM
        if (entry->used_mask == 0)
        {
            PmmFreeFrame(page_pa);
            if (prev)
            {
                prev->next = entry->next;
            }
            else
            {
                pool_head = entry->next;
            }
            KSlabFree(&l2_entry_cache, entry);
        }
        return;
    }

}