#include "l2_pool.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/mmu/mmu.h"  // for PA_TO_VA
#include "lib/mem.h"     

static l2_pool_entry_t pool[MAX_L2_PAGES];
static int pool_count = 0;

uintptr_t l2_pool_alloc(void)
{
    // 1) Try to find a free slot in an existing page
    for (int i = 0; i < pool_count; i++)
    {
        if (pool[i].used_mask == 0xF)
            continue;  // all 4 slots occupied

        for (int slot = 0; slot < 4; slot++)
        {
            if (!(pool[i].used_mask & (1 << slot)))
            {
                pool[i].used_mask |= (1 << slot);
                uintptr_t pa = pool[i].page_pa + (slot * 1024);
                memset((void *)PA_TO_VA(pa), 0, 1024);
                return pa;
            }
        }
    }

    // 2) No free slots — need a new page from PMM
    if (pool_count == MAX_L2_PAGES)
        return 0;  // pool exhausted

    uintptr_t page_pa = pmm_alloc_page();
    if (!page_pa)
        return 0;  // out of physical memory

    memset((void *)PA_TO_VA(page_pa), 0, 4096);  // zero whole page

    pool[pool_count].page_pa   = page_pa;
    pool[pool_count].used_mask = 0x1;  // slot 0 claimed
    pool_count++;

    return page_pa;  // slot 0 is at offset 0
}

void l2_pool_free(uintptr_t l2_pa)
{
    if (!l2_pa)
        return;

    uintptr_t page_pa = l2_pa & ~0xFFF;
    int slot = (l2_pa & 0xFFF) / 1024;

    for (int i = 0; i < pool_count; i++)
    {
        if (pool[i].page_pa != page_pa)
            continue;

        pool[i].used_mask &= ~(1 << slot);

        // If all 4 slots free, return page to PMM
        if (pool[i].used_mask == 0)
        {
            pmm_free_page(page_pa);
            // Swap with last entry to keep array compact
            pool[i] = pool[pool_count - 1];
            pool_count--;
        }
        return;
    }
}