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


PmmState pmmState;
extern kernel_layout_t kernel_layout;
extern void syspage_update_mem(void);
spinlock_t pmm_lock = SPINLOCK_INIT;

static bool pmm_is_valid_managed_pa(PhysAddr pa)
{
    if (pa == 0)
        return true; // freelist terminator
    if ((pa % PAGE_SIZE) != 0)
        return false;

    const Pfn pfn = PhysToPfn(pa);
    return (pfn >= pmmState.pfn_base && pfn < pmmState.pfn_end);
}

static void pmm_rebuild_freelist(void)
{
    pmmState.freelist_head = 0;
    for (size_t i = 0; i < pmmState.total_frames; i++)
    {
        size_t byte_idx = i / 8;
        size_t bit_idx = i % 8;
        if (!(pmmState.bitmap[byte_idx] & (1u << bit_idx)))
        {
            PhysAddr pa = PfnToPhys(pmmState.pfn_base + i);
            PhysAddr *page_va = (PhysAddr *)PA_TO_VA(pa);
            *page_va = pmmState.freelist_head;
            pmmState.freelist_head = pa;
        }
    }
}

/* Remove any free-list nodes whose PA is within [start_pa, end_pa). */
static void pmm_freelist_remove_range(PhysAddr start_pa, PhysAddr end_pa)
{
    PhysAddr prev_pa = 0;
    PhysAddr curr_pa = pmmState.freelist_head;

    while (curr_pa)
    {
        if (!pmm_is_valid_managed_pa(curr_pa))
        {
            pmm_rebuild_freelist();
            prev_pa = 0;
            curr_pa = pmmState.freelist_head;
            continue;
        }

        PhysAddr next_pa = *(PhysAddr *)PA_TO_VA(curr_pa);;

        if (!pmm_is_valid_managed_pa(next_pa))
        {
            pmm_rebuild_freelist();
            prev_pa = 0;
            curr_pa = pmmState.freelist_head;
            continue;
        }

        if (curr_pa >= start_pa && curr_pa < end_pa)
        {
            if (prev_pa == 0)
            {
                pmmState.freelist_head = next_pa;
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
    if (pmmState.freelist_head == 0)
        return (PhysAddr)0;

    if (!pmm_is_valid_managed_pa(pmmState.freelist_head))
    {
        pmm_rebuild_freelist();
        if (pmmState.freelist_head == 0)
            return (PhysAddr)0;
    }

    PhysAddr pa = pmmState.freelist_head;

    /* Pop: read next pointer stored in the page itself */
    PhysAddr *page_va = (PhysAddr *)PA_TO_VA(pa);
    PhysAddr next_pa = *page_va;
    if (!pmm_is_valid_managed_pa(next_pa))
    {
        pmm_rebuild_freelist();
        if (pmmState.freelist_head == 0)
            return (PhysAddr)0;
        pa = pmmState.freelist_head;
        page_va = (PhysAddr *)PA_TO_VA(pa);
        next_pa = *page_va;
        if (!pmm_is_valid_managed_pa(next_pa))
        {
            pmmState.freelist_head = 0;
            return (PhysAddr)0;
        }
    }

    pmmState.freelist_head = next_pa;

    /* Keep bitmap in sync */
    size_t index = PhysToPfn(pa) - pmmState.pfn_base;
    size_t byte_idx = index / 8;
    size_t bit_idx = index % 8;

    assert(byte_idx < pmmState.bitmap_bytes);
    assert(!(pmmState.bitmap[byte_idx] & (1u << bit_idx))); /* must be free in bitmap */

    pmmState.bitmap[byte_idx] |= (uint8_t)(1u << bit_idx);
    pmmState.free_frames--;
    assert(pmmState.free_frames <= pmmState.total_frames);

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
    pmmState.pfn_base = PhysToPfn(kernel_layout.ram_start);
    pmmState.pfn_end = PhysToPfn(kernel_layout.ram_end);
    pmmState.total_frames = pmmState.pfn_end - pmmState.pfn_base;
    pmmState.free_frames = pmmState.total_frames;

    // Place bitmap after kernel, page-aligned
    PhysAddr bitmap_start_pa = align_up(kernel_layout.kernel_end_pa, PAGE_SIZE);
    size_t bitmap_bytes = (pmmState.total_frames + 7) / 8;
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
    pmmState.bitmap = kernel_layout.bitmap_va;
    pmmState.bitmap_bytes = bitmap_bytes;
    memset(pmmState.bitmap, 0, bitmap_size);

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

    Pfn start_pfn = PhysToPfn(astart);
    Pfn end_pfn = PhysToPfn(aend);

    /* PFN bounds check (pfn_end is exclusive) */
    if (start_pfn < pmmState.pfn_base || end_pfn > pmmState.pfn_end)
    {
        return ERR_BADARG;
    }

    assert(pmmState.bitmap != NULL);
    assert(pmmState.pfn_end > pmmState.pfn_base);
    assert(pmmState.total_frames == (size_t)(pmmState.pfn_end - pmmState.pfn_base));
    assert(pmmState.bitmap_bytes * 8ULL >= pmmState.total_frames);

    for (Pfn pfn = start_pfn; pfn < end_pfn; pfn++)
    {
        size_t index = pfn - pmmState.pfn_base;
        size_t byte_idx = index / 8;
        size_t bit_idx = index % 8;

        /* safety: ensure we do not walk past bitmap */
        assert(byte_idx < pmmState.bitmap_bytes);
        if (byte_idx >= pmmState.bitmap_bytes)
            break;

        uint8_t mask = (uint8_t)(1u << bit_idx);

        /* Only flip and update counters if bit was previously 0 */
        if (!(pmmState.bitmap[byte_idx] & mask))
        {
            pmmState.bitmap[byte_idx] |= mask;
            if (pmmState.free_frames > 0)
                pmmState.free_frames--;
            assert(pmmState.free_frames <= pmmState.total_frames);
        }
    }

    return ZUZU_OK;
}

/* unmark: mark pages in [start, end) as FREE */
Err PmmUnmarkRange(const PhysAddr start, const PhysAddr end)
{
    if (start >= end)
        return ERR_BADARG;

    const PhysAddr astart = align_down(start, PAGE_SIZE);
    const PhysAddr aend = align_up(end, PAGE_SIZE);

    const Pfn start_pfn = PhysToPfn(astart);
    Pfn end_pfn = PhysToPfn(aend);

    if (start_pfn < pmmState.pfn_base || end_pfn > pmmState.pfn_end)
    {
        return ERR_BADARG;
    }

    assert(pmmState.bitmap != NULL);
    assert(pmmState.pfn_end > pmmState.pfn_base);
    assert(pmmState.total_frames == (size_t)(pmmState.pfn_end - pmmState.pfn_base));
    assert(pmmState.bitmap_bytes * 8ULL >= pmmState.total_frames);

    for (Pfn pfn = start_pfn; pfn < end_pfn; pfn++)
    {
        const size_t index = pfn - pmmState.pfn_base;
        const size_t byte_idx = index / 8;
        const size_t bit_idx = index % 8;

        assert(byte_idx < pmmState.bitmap_bytes);
        if (byte_idx >= pmmState.bitmap_bytes)
            break;

        const uint8_t mask = (uint8_t)(1u << bit_idx);

        /* Only flip and update counters if bit was previously 1 */
        if (pmmState.bitmap[byte_idx] & mask)
        {
            pmmState.bitmap[byte_idx] &= ~mask;
            if (pmmState.free_frames < pmmState.total_frames)
                pmmState.free_frames++;
            assert(pmmState.free_frames <= pmmState.total_frames);
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
    if (n_frames == 0 || pmmState.free_frames < n_frames)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return PHYS_NULL;
    }

    assert(pmmState.bitmap != NULL);
    assert(pmmState.total_frames == (size_t)(pmmState.pfn_end - pmmState.pfn_base));
    assert(pmmState.bitmap_bytes * 8ULL >= pmmState.total_frames);
    assert(pmmState.free_frames <= pmmState.total_frames);
    assert(n_frames <= pmmState.total_frames);

    size_t total_pages = pmmState.total_frames;
    size_t consecutive = 0;
    size_t start_index = 0;

    for (size_t index = 0; index < total_pages; index++)
    {
        size_t byte_idx = index / 8;
        size_t bit_idx = index % 8;
        uint8_t mask = (uint8_t)(1u << bit_idx);

        if (byte_idx >= pmmState.bitmap_bytes)
        {
            break; /* beyond managed pages */
        }

        if (!(pmmState.bitmap[byte_idx] & mask))
        { /* free */
            if (consecutive == 0)
            {
                start_index = index;
            }
            consecutive++;

            if (consecutive == n_frames)
            {
                /* Mark pages as allocated */
                PhysAddr start_pa = PfnToPhys(pmmState.pfn_base + start_index);
                PhysAddr end_pa = PfnToPhys(pmmState.pfn_base + start_index + n_frames);
                if (PmmMarkRange(start_pa, end_pa) != ZUZU_OK)
                {
                    spin_unlock_irqrestore(&pmm_lock, flags);
                    return PHYS_NULL; /* marking failed */
                }

                /* Keep freelist in sync without a full O(total_pages) rebuild. */
                pmm_freelist_remove_range(start_pa, end_pa);

                Pfn pfn = pmmState.pfn_base + start_index;
                PhysAddr addr = PfnToPhys(pfn);
                assert(addr % PAGE_SIZE == 0);
                assert(pfn >= pmmState.pfn_base && (pfn + n_frames) <= pmmState.pfn_end);
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
    return PHYS_NULL;
}

void PmmFreeFrame(const PhysAddr addr)
{
    uint32_t flags;
#ifdef PMM_TRACE
    KTRACE("free_page pa=%p caller: %s", (void *)addr, ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    spin_lock_irqsave(&pmm_lock, &flags);
    assert(addr % PAGE_SIZE == 0);

    const Pfn pfn = PhysToPfn(addr);

    /* bounds: pfn must be inside [pfn_base, pfn_end) */
    assert(pfn >= pmmState.pfn_base && pfn < pmmState.pfn_end);

    size_t index = pfn - pmmState.pfn_base;
    size_t byte_idx = index / 8;
    size_t bit_idx = index % 8;

    assert(byte_idx < pmmState.bitmap_bytes);

    assert(pmmState.bitmap != NULL);
    assert(pmmState.free_frames <= pmmState.total_frames);

    const uint8_t mask = (uint8_t)(1u << bit_idx);

    /* if bit set -> allocated -> free it */
    if (pmmState.bitmap[byte_idx] & mask)
    {
        pmmState.bitmap[byte_idx] &= ~mask;
        pmmState.free_frames++;
        assert(pmmState.free_frames <= pmmState.total_frames);

        /* Push onto freelist */
        PhysAddr *page_va = (PhysAddr *)PA_TO_VA(addr);
        *page_va = pmmState.freelist_head;
        pmmState.freelist_head = addr;

        syspage_update_mem();
        spin_unlock_irqrestore(&pmm_lock, flags);
        return;
    }

    /* already free -> double free: a live bug, not a bad argument */
    spin_unlock_irqrestore(&pmm_lock, flags);
    panic("pmm: double free of frame %#lx", pfn);
}

uintptr_t PmmAllocFramesContigAligned(const size_t n_frames, size_t align_frames)
{
    uint32_t flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    if (n_frames == 0)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return PHYS_NULL;
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
        return PHYS_NULL;
    }

    if (pmmState.free_frames < n_frames)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return PHYS_NULL;
    }

    assert(pmmState.bitmap != NULL);
    assert(pmmState.total_frames == (size_t)(pmmState.pfn_end - pmmState.pfn_base));
    assert(pmmState.bitmap_bytes * 8ULL >= pmmState.total_frames);

    const size_t total_pages = pmmState.total_frames;
    size_t consecutive = 0;
    size_t start_index = 0;

    for (size_t index = 0; index < total_pages; index++)
    {

        // Enforce alignment on the start of a run
        if (consecutive == 0)
        {
            if (((pmmState.pfn_base + index) & (align_frames - 1)) != 0)
            {
                continue;
            }
        }

        size_t byte_idx = index / 8;
        size_t bit_idx = index % 8;
        uint8_t mask = (uint8_t)(1u << bit_idx);

        if (byte_idx >= pmmState.bitmap_bytes)
            break;

        if (!(pmmState.bitmap[byte_idx] & mask))
        { // free
            if (consecutive == 0)
                start_index = index;
            consecutive++;

            if (consecutive == n_frames)
            {
                const uintptr_t start_pa = PfnToPhys(pmmState.pfn_base + start_index);
                const uintptr_t end_pa = PfnToPhys(pmmState.pfn_base + start_index + n_frames);

                if (PmmMarkRange(start_pa, end_pa) != ZUZU_OK)
                {
                    spin_unlock_irqrestore(&pmm_lock, flags);
                    return PHYS_NULL;
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
    return PHYS_NULL;
}

size_t PmmAllocFramesScattered(const size_t n_frames, PhysAddr *out_addrs)
{
#ifdef PMM_TRACE
    KTRACE("alloc_pages_scattered n=%zu caller: %s", n_frames, ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    uint32_t flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    if (n_frames == 0 || pmmState.free_frames < n_frames)
    {
        spin_unlock_irqrestore(&pmm_lock, flags);
        return 0;
    }
    assert(pmmState.bitmap != NULL);
    assert(pmmState.total_frames == (size_t)(pmmState.pfn_end - pmmState.pfn_base));
    assert(pmmState.bitmap_bytes * 8ULL >= pmmState.total_frames);
    assert(pmmState.free_frames <= pmmState.total_frames);
    assert(n_frames <= pmmState.total_frames);
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