#include "kernel/mm/pmm.h"
#include <assert.h>
#include "kernel/layout.h"
#include "kernel/dtb/dtb.h"
#include <arch/symbols.h>
#include <string.h>
#include "kernel/mm/vmm.h" // PA_TO_VA / VA_TO_PA helpers
#include <spinlock.h>
#include <zuzu/types.h>
#include "core/panic.h"

#ifdef PMM_TRACE
#include <core/ksym.h>
#endif

#define LOG_FMT(fmt) "(pmm) " fmt
#include "core/log.h"


PmmState pmm_state;
extern kernel_layout_t kernel_layout;
extern void syspage_update_mem(void);
spinlock_t pmm_lock = SPINLOCK_INIT;

static bool pmm_is_valid_managed_pa(PhysAddr pa)
{
    if (pa == 0)
        return true; // freelist terminator
    if ((pa % PAGE_SIZE) != 0)
        return false;

    const size_t pfn = pa / PAGE_SIZE;
    return (pfn >= pmm_state.pfn_base && pfn < pmm_state.pfn_end);
}

static void pmm_rebuild_freelist(void)
{
    pmm_state.freelist_head = 0;
    for (size_t i = 0; i < pmm_state.total_pages; i++)
    {
        size_t byte_idx = i / 8;
        size_t bit_idx = i % 8;
        if (!(pmm_state.bitmap[byte_idx] & (1u << bit_idx)))
        {
            PhysAddr pa = (pmm_state.pfn_base + i) * PAGE_SIZE;
            PhysAddr *page_va = (PhysAddr *)PA_TO_VA(pa);
            *page_va = pmm_state.freelist_head;
            pmm_state.freelist_head = pa;
        }
    }
}

/* Remove any free-list nodes whose PA is within [start_pa, end_pa). */
static void pmm_freelist_remove_range(PhysAddr start_pa, PhysAddr end_pa)
{
    PhysAddr prev_pa = 0;
    PhysAddr curr_pa = pmm_state.freelist_head;

    while (curr_pa)
    {
        if (!pmm_is_valid_managed_pa(curr_pa))
        {
            pmm_rebuild_freelist();
            prev_pa = 0;
            curr_pa = pmm_state.freelist_head;
            continue;
        }

        PhysAddr next_pa = *(PhysAddr *)PA_TO_VA(curr_pa);;

        if (!pmm_is_valid_managed_pa(next_pa))
        {
            pmm_rebuild_freelist();
            prev_pa = 0;
            curr_pa = pmm_state.freelist_head;
            continue;
        }

        if (curr_pa >= start_pa && curr_pa < end_pa)
        {
            if (prev_pa == 0)
            {
                pmm_state.freelist_head = next_pa;
            }
            else
            {
                *(PhysAddr *)PA_TO_VA(prev_pa) = next_pa;
            }
        }
        else
        {
            prev_pa = curr_pa;
        }

        curr_pa = next_pa;
    }
}

static PhysAddr pmm_alloc_frame_locked(void)
{
    if (pmm_state.freelist_head == 0)
        return (PhysAddr)0;

    if (!pmm_is_valid_managed_pa(pmm_state.freelist_head))
    {
        pmm_rebuild_freelist();
        if (pmm_state.freelist_head == 0)
            return (PhysAddr)0;
    }

    PhysAddr pa = pmm_state.freelist_head;

    /* Pop: read next pointer stored in the page itself */
    PhysAddr *page_va = (PhysAddr *)PA_TO_VA(pa);
    PhysAddr next_pa = *page_va;
    if (!pmm_is_valid_managed_pa(next_pa))
    {
        pmm_rebuild_freelist();
        if (pmm_state.freelist_head == 0)
            return (PhysAddr)0;
        pa = pmm_state.freelist_head;
        page_va = (PhysAddr *)PA_TO_VA(pa);
        next_pa = *page_va;
        if (!pmm_is_valid_managed_pa(next_pa))
        {
            pmm_state.freelist_head = 0;
            return (PhysAddr)0;
        }
    }

    pmm_state.freelist_head = next_pa;

    /* Keep bitmap in sync */
    size_t index = (pa / PAGE_SIZE) - pmm_state.pfn_base;
    size_t byte_idx = index / 8;
    size_t bit_idx = index % 8;

    assert(byte_idx < pmm_state.bitmap_bytes);
    assert(!(pmm_state.bitmap[byte_idx] & (1u << bit_idx))); /* must be free in bitmap */

    pmm_state.bitmap[byte_idx] |= (uint8_t)(1u << bit_idx);
    pmm_state.free_pages--;
    assert(pmm_state.free_pages <= pmm_state.total_pages);

    return pa;
}

static void pmm_reserve_boot_regions(void)
{
    PmmMarkRange((PhysAddr)_boot_start, (PhysAddr)_boot_end);
    /* The DTB is wherever the bootloader put it, not necessarily adjacent
     * to the kernel (QEMU's Linux-boot path and the Pi firmware both place
     * it independently). Reserve its actual extent. */
    PmmMarkRange(kernel_layout.dtb_start_pa,
                   kernel_layout.dtb_start_pa + dtb_total_size());
    PmmMarkRange(kernel_layout.kernel_start_pa, kernel_layout.kernel_end_pa);
    PmmMarkRange(kernel_layout.bitmap_start_pa, kernel_layout.bitmap_end_pa);

    // All mode stacks
    PmmMarkRange((PhysAddr)__stack_region_base__, (PhysAddr)__stack_region_end__);

    /* Firmware /memreserve/ ranges (e.g. secondary-core spin tables on the
     * Pi 4). Ranges outside managed RAM are rejected by pmm_mark_range. */
    uint64_t rsv_addr, rsv_size;
    for (uint32_t i = 0; dtb_get_memrsv(i, &rsv_addr, &rsv_size); i++)
        PmmMarkRange((PhysAddr)rsv_addr, (PhysAddr)(rsv_addr + rsv_size));

    /* A bootloader-supplied initrd (DTB /chosen) lives outside the kernel
     * image, wherever it was loaded, so it needs its own reservation. */
    uint64_t initrd_start, initrd_end;
    if (dtb_get_chosen_initrd(&initrd_start, &initrd_end))
        PmmMarkRange((PhysAddr)initrd_start, (PhysAddr)initrd_end);
}

void PmmInit(void)
{
    // Compute PFN range from phys_region
    pmm_state.pfn_base = kernel_layout.ram_start / PAGE_SIZE;
    pmm_state.pfn_end = kernel_layout.ram_end / PAGE_SIZE;
    pmm_state.total_pages = pmm_state.pfn_end - pmm_state.pfn_base;
    pmm_state.free_pages = pmm_state.total_pages;

    // Place bitmap after kernel, page-aligned
    PhysAddr bitmap_start_pa = align_up(kernel_layout.kernel_end_pa, PAGE_SIZE);
    size_t bitmap_bytes = (pmm_state.total_pages + 7) / 8;
    size_t bitmap_size = align_up(bitmap_bytes, PAGE_SIZE);
    PhysAddr bitmap_end_pa = bitmap_start_pa + bitmap_size;

    // Sanity checks
    assert(bitmap_end_pa <= kernel_layout.stack_base_pa);
    assert(bitmap_end_pa <= kernel_layout.ram_end);

    // Record in layout (physical placement)
    kernel_layout.bitmap_start_pa = bitmap_start_pa;
    kernel_layout.bitmap_end_pa = bitmap_end_pa;

    // Establish dereferenceable VA for the bitmap.
    // After identity mapping is removed, the bitmap MUST be accessed via VA.
    kernel_layout.bitmap_va = (uint8_t *)PA_TO_VA(kernel_layout.bitmap_start_pa);

    // Install and zero (use VA pointer)
    pmm_state.bitmap = kernel_layout.bitmap_va;
    pmm_state.bitmap_bytes = bitmap_bytes;
    memset(pmm_state.bitmap, 0, bitmap_size);

    // Reserve boot-time regions
    pmm_reserve_boot_regions();

    // Build the freelist from all free pages in the bitmap
    pmm_rebuild_freelist();
}

/* mark: mark pages in [start, end) as USED */
Err PmmMarkRange(PhysAddr start, PhysAddr end)
{
    if (start >= end)
        return ERR_BADARG;

    /* Align the range to page boundaries */
    PhysAddr astart = align_down(start, PAGE_SIZE);
    PhysAddr aend = align_up(end, PAGE_SIZE);

    size_t start_pfn = astart / PAGE_SIZE;
    size_t end_pfn = aend / PAGE_SIZE;

    /* PFN bounds check (pfn_end is exclusive) */
    if (start_pfn < pmm_state.pfn_base || end_pfn > pmm_state.pfn_end)
    {
        return ERR_BADARG;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.pfn_end > pmm_state.pfn_base);
    assert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);

    for (size_t pfn = start_pfn; pfn < end_pfn; pfn++)
    {
        size_t index = pfn - pmm_state.pfn_base;
        size_t byte_idx = index / 8;
        size_t bit_idx = index % 8;

        /* safety: ensure we do not walk past bitmap */
        assert(byte_idx < pmm_state.bitmap_bytes);
        if (byte_idx >= pmm_state.bitmap_bytes)
            break;

        uint8_t mask = (uint8_t)(1u << bit_idx);

        /* Only flip and update counters if bit was previously 0 */
        if (!(pmm_state.bitmap[byte_idx] & mask))
        {
            pmm_state.bitmap[byte_idx] |= mask;
            if (pmm_state.free_pages > 0)
                pmm_state.free_pages--;
            assert(pmm_state.free_pages <= pmm_state.total_pages);
        }
    }

    return ZUZU_OK;
}

/* unmark: mark pages in [start, end) as FREE */
Err PmmUnmarkRange(PhysAddr start, PhysAddr end)
{
    if (start >= end)
        return ERR_BADARG;

    const PhysAddr astart = align_down(start, PAGE_SIZE);
    const PhysAddr aend = align_up(end, PAGE_SIZE);

    const size_t start_pfn = astart / PAGE_SIZE;
    size_t end_pfn = aend / PAGE_SIZE;

    if (start_pfn < pmm_state.pfn_base || end_pfn > pmm_state.pfn_end)
    {
        return ERR_BADARG;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.pfn_end > pmm_state.pfn_base);
    assert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);

    for (size_t pfn = start_pfn; pfn < end_pfn; pfn++)
    {
        const size_t index = pfn - pmm_state.pfn_base;
        const size_t byte_idx = index / 8;
        const size_t bit_idx = index % 8;

        assert(byte_idx < pmm_state.bitmap_bytes);
        if (byte_idx >= pmm_state.bitmap_bytes)
            break;

        const uint8_t mask = (uint8_t)(1u << bit_idx);

        /* Only flip and update counters if bit was previously 1 */
        if (pmm_state.bitmap[byte_idx] & mask)
        {
            pmm_state.bitmap[byte_idx] &= ~mask;
            if (pmm_state.free_pages < pmm_state.total_pages)
                pmm_state.free_pages++;
            assert(pmm_state.free_pages <= pmm_state.total_pages);
        }
    }

    return ZUZU_OK;
}

PhysAddr PmmAllocFrame(void)
{
    uint32_t flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    PhysAddr pa = pmm_alloc_frame_locked();
    if (pa != 0)
    {
        syspage_update_mem();
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
#ifdef PMM_TRACE
    KTRACE("alloc_page pa=%p caller: %s", (void *)pa, ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    return pa;
}
PhysAddr PmmAllocFramesContig(size_t n_frames)
{
    uint32_t flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    if (n_frames == 0 || pmm_state.free_pages < n_frames)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return (PhysAddr)0;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);
    assert(pmm_state.free_pages <= pmm_state.total_pages);
    assert(n_frames <= pmm_state.total_pages);

    size_t total_pages = pmm_state.total_pages;
    size_t consecutive = 0;
    size_t start_index = 0;

    for (size_t index = 0; index < total_pages; index++)
    {
        size_t byte_idx = index / 8;
        size_t bit_idx = index % 8;
        uint8_t mask = (uint8_t)(1u << bit_idx);

        if (byte_idx >= pmm_state.bitmap_bytes)
        {
            break; /* beyond managed pages */
        }

        if (!(pmm_state.bitmap[byte_idx] & mask))
        { /* free */
            if (consecutive == 0)
            {
                start_index = index;
            }
            consecutive++;

            if (consecutive == n_frames)
            {
                /* Mark pages as allocated */
                PhysAddr start_pa = start_index * PAGE_SIZE + pmm_state.pfn_base * PAGE_SIZE;
                PhysAddr end_pa = (start_index + n_frames) * PAGE_SIZE + pmm_state.pfn_base * PAGE_SIZE;
                if (PmmMarkRange(start_pa, end_pa) != ZUZU_OK)
                {
                    spin_unlock_irqrestore(&pmm_lock, flags);
                    return (PhysAddr)0; /* marking failed */
                }

                /* Keep freelist in sync without a full O(total_pages) rebuild. */
                pmm_freelist_remove_range(start_pa, end_pa);

                size_t pfn = pmm_state.pfn_base + start_index;
                PhysAddr addr = (PhysAddr)pfn * PAGE_SIZE;
                assert(addr % PAGE_SIZE == 0);
                assert(pfn >= pmm_state.pfn_base && (pfn + n_frames) <= pmm_state.pfn_end);
                syspage_update_mem(); // update free memory info in syspage
                spin_unlock_irqrestore(&pmm_lock, flags);
#ifdef PMM_TRACE
                KTRACE("alloc_pages n=%zu pa=%p caller: %s", n_frames, (void *)addr, ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
                return addr;
            }
        }
        else
        {
            consecutive = 0; /* reset */
        }
    }

    spin_unlock_irqrestore(&pmm_lock, flags);
    return (PhysAddr)0;
}

Err PmmFreeFrame(const PhysAddr addr)
{
    uint32_t flags;
#ifdef PMM_TRACE
    KTRACE("free_page pa=%p caller: %s", (void *)addr, ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    spin_lock_irqsave(&pmm_lock, &flags);
    if (addr % PAGE_SIZE != 0)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return ERR_BADARG;
    }

    const size_t pfn = addr / PAGE_SIZE;

    /* bounds: pfn must be inside [pfn_base, pfn_end) */
    if (pfn < pmm_state.pfn_base || pfn >= pmm_state.pfn_end)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return ERR_BADARG;
    }

    size_t index = pfn - pmm_state.pfn_base;
    size_t byte_idx = index / 8;
    size_t bit_idx = index % 8;

    if (byte_idx >= pmm_state.bitmap_bytes)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return ERR_BADARG;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.free_pages <= pmm_state.total_pages);

    const uint8_t mask = (uint8_t)(1u << bit_idx);

    /* if bit set -> allocated -> free it */
    if (pmm_state.bitmap[byte_idx] & mask)
    {
        pmm_state.bitmap[byte_idx] &= ~mask;
        pmm_state.free_pages++;
        assert(pmm_state.free_pages <= pmm_state.total_pages);

        /* Push onto freelist */
        PhysAddr *page_va = (PhysAddr *)PA_TO_VA(addr);
        *page_va = pmm_state.freelist_head;
        pmm_state.freelist_head = addr;

        syspage_update_mem();
        spin_unlock_irqrestore(&pmm_lock, flags);
        return ZUZU_OK;
    }

    /* already free -> double free: a live bug, not a bad argument */
    spin_unlock_irqrestore(&pmm_lock, flags);
    panic("PmmFreeFrame: double free of pa=0x%08X", addr);
}

uintptr_t PmmAllocFramesContigAligned(const size_t n_frames, size_t align_frames)
{
    uint32_t flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    if (n_frames == 0)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return (uintptr_t)0;
    }
    if (align_frames == 0)
        align_frames = 1;
#ifdef PMM_TRACE
    KTRACE("alloc_pages_aligned n=%zu align=%zu caller: %s", n_frames, align_frames, ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    // Require power-of-two alignment (common + cheap)
    if ((align_frames & (align_frames - 1)) != 0)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return (PhysAddr)0;
    }

    if (pmm_state.free_pages < n_frames)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return (PhysAddr)0;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);

    const size_t total_pages = pmm_state.total_pages;
    size_t consecutive = 0;
    size_t start_index = 0;

    for (size_t index = 0; index < total_pages; index++)
    {

        // Enforce alignment on the start of a run
        if (consecutive == 0)
        {
            if (((pmm_state.pfn_base + index) & (align_frames - 1)) != 0)
            {
                continue;
            }
        }

        size_t byte_idx = index / 8;
        size_t bit_idx = index % 8;
        uint8_t mask = (uint8_t)(1u << bit_idx);

        if (byte_idx >= pmm_state.bitmap_bytes)
            break;

        if (!(pmm_state.bitmap[byte_idx] & mask))
        { // free
            if (consecutive == 0)
                start_index = index;
            consecutive++;

            if (consecutive == n_frames)
            {
                const uintptr_t start_pa = (uintptr_t)(pmm_state.pfn_base + start_index) * PAGE_SIZE;
                const uintptr_t end_pa = (uintptr_t)(pmm_state.pfn_base + start_index + n_frames) * PAGE_SIZE;

                if (PmmMarkRange(start_pa, end_pa) != ZUZU_OK)
                {
                    spin_unlock_irqrestore(&pmm_lock, flags);
                    return (uintptr_t)0;
                }

                /* Keep freelist in sync without a full O(total_pages) rebuild. */
                pmm_freelist_remove_range(start_pa, end_pa);

                syspage_update_mem(); // update free memory info in syspage
                spin_unlock_irqrestore(&pmm_lock, flags);
                return start_pa;
            }
        }
        else
        {
            consecutive = 0;
        }
    }

    spin_unlock_irqrestore(&pmm_lock, flags);
    return (PhysAddr)0;
}

size_t PmmAllocFramesScattered(const size_t n_frames, PhysAddr *out_addrs)
{
#ifdef PMM_TRACE
    KTRACE("alloc_pages_scattered n=%zu caller: %s", n_frames, ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    uint32_t flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    if (n_frames == 0 || pmm_state.free_pages < n_frames)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return 0;
    }
    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);
    assert(pmm_state.free_pages <= pmm_state.total_pages);
    assert(n_frames <= pmm_state.total_pages);
    if (!n_frames || !out_addrs)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return 0;
    }
    for (size_t i = 0; i < n_frames; i++)
    {
        const PhysAddr new_page = pmm_alloc_frame_locked();
        if (new_page == 0)
        {
            syspage_update_mem();
            spin_unlock_irqrestore(&pmm_lock, flags);
            return i;
        }
        out_addrs[i] = new_page;
    }
    syspage_update_mem(); // update free memory info in syspage
    spin_unlock_irqrestore(&pmm_lock, flags);
    return n_frames;
}