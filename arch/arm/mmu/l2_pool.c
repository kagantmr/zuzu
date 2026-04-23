#include "l2_pool.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/alloc.h"
#include "arch/arm/mmu/mmu.h"  // for PA_TO_VA
#include <mem.h>
#include <spinlock.h>

static l2_pool_entry_t *pool_head = NULL;
spinlock_t l2_pool_lock = SPINLOCK_INIT;

uintptr_t l2_pool_alloc(void)
{
    uint32_t flags;
    spin_lock_irqsave(&l2_pool_lock, &flags);
    for (l2_pool_entry_t *entry = pool_head; entry; entry = entry->next)
    {
        if (entry->used_mask == 0xF)
            continue;  // all 4 slots occupied

        for (int slot = 0; slot < 4; slot++)
        {
            if (!(entry->used_mask & (1 << slot)))
            {
                entry->used_mask |= (1 << slot);
                uintptr_t pa = entry->page_pa + (slot * 1024);
                memset((void *)PA_TO_VA(pa), 0, 1024);
                spin_unlock_irqrestore(&l2_pool_lock, flags);
                return pa;
            }
        }
    }

    uintptr_t page_pa = pmm_alloc_page();
    if (!page_pa) {
        spin_unlock_irqrestore(&l2_pool_lock, flags);
        return 0;  // out of physical memory
    }

    l2_pool_entry_t *entry = kmalloc(sizeof(l2_pool_entry_t));
    if (!entry) {
        pmm_free_page(page_pa);
        spin_unlock_irqrestore(&l2_pool_lock, flags);
        return 0;
    }

    memset((void *)PA_TO_VA(page_pa), 0, 4096);  // zero whole page

    entry->page_pa = page_pa;
    entry->used_mask = 0x1;  // slot 0 claimed
    entry->next = pool_head;
    pool_head = entry;

    spin_unlock_irqrestore(&l2_pool_lock, flags);
    return page_pa;  // slot 0 is at offset 0
}

void l2_pool_free(uintptr_t l2_pa)
{
    uint32_t flags;
    spin_lock_irqsave(&l2_pool_lock, &flags);
    if (!l2_pa) {
        spin_unlock_irqrestore(&l2_pool_lock, flags);
        return;
    }

    uintptr_t page_pa = l2_pa & ~0xFFF;
    int slot = (l2_pa & 0xFFF) / 1024;

    l2_pool_entry_t *prev = NULL;
    l2_pool_entry_t *entry = pool_head;

    while (entry)
    {
        if (entry->page_pa != page_pa) {
            prev = entry;
            entry = entry->next;
            continue;
        }

        entry->used_mask &= ~(1 << slot);

        // If all 4 slots free, return page to PMM
        if (entry->used_mask == 0)
        {
            pmm_free_page(page_pa);
            if (prev) {
                prev->next = entry->next;
            } else {
                pool_head = entry->next;
            }
            kfree(entry);
        }
        spin_unlock_irqrestore(&l2_pool_lock, flags);
        return;
    }

    spin_unlock_irqrestore(&l2_pool_lock, flags);
}