#include "kernel/mm/pmm.h"
#include "core/assert.h"
#include "kernel/layout.h"
#include "arch/arm/include/symbols.h"
#include "lib/mem.h"
#include "kernel/vmm/vmm.h"   // PA_TO_VA / VA_TO_PA helpers

pmm_state_t pmm_state;
extern phys_region_t phys_region;
extern kernel_layout_t kernel_layout;

static void pmm_reserve_boot_regions(void) {
    pmm_mark_range(kernel_layout.dtb_start_pa,  kernel_layout.kernel_start_pa);
    pmm_mark_range(kernel_layout.kernel_start_pa, kernel_layout.kernel_end_pa);
    pmm_mark_range(kernel_layout.bitmap_start_pa, kernel_layout.bitmap_end_pa);
    
    // All mode stacks
    pmm_mark_range((uintptr_t)__svc_stack_base__, (uintptr_t)__svc_stack_top__);
    pmm_mark_range((uintptr_t)__irq_stack_base__, (uintptr_t)__irq_stack_top__);
    pmm_mark_range((uintptr_t)__abt_stack_base__, (uintptr_t)__abt_stack_top__);
    pmm_mark_range((uintptr_t)__und_stack_base__, (uintptr_t)__und_stack_top__);
}

void pmm_init(void) {
    // Compute PFN range from phys_region
    pmm_state.pfn_base    = phys_region.start / PAGE_SIZE;
    pmm_state.pfn_end     = phys_region.end / PAGE_SIZE;
    pmm_state.total_pages = pmm_state.pfn_end - pmm_state.pfn_base;
    pmm_state.free_pages  = pmm_state.total_pages;

    // Place bitmap after kernel, page-aligned
    uintptr_t bitmap_start_pa = align_up(kernel_layout.kernel_end_pa, PAGE_SIZE);
    size_t bitmap_bytes    = (pmm_state.total_pages + 7) / 8;
    size_t bitmap_size     = align_up(bitmap_bytes, PAGE_SIZE);
    uintptr_t bitmap_end_pa   = bitmap_start_pa + bitmap_size;

    // Sanity checks
    kassert(bitmap_end_pa <= kernel_layout.stack_base_pa);
    kassert(bitmap_end_pa <= phys_region.end);

    // Record in layout (physical placement)
    kernel_layout.bitmap_start_pa = bitmap_start_pa;
    kernel_layout.bitmap_end_pa   = bitmap_end_pa;

    // Establish dereferenceable VA for the bitmap.
    // After identity mapping is removed, the bitmap MUST be accessed via VA.
    kernel_layout.bitmap_va = (uint8_t *)PA_TO_VA(kernel_layout.bitmap_start_pa);

    // Install and zero (use VA pointer)
    pmm_state.bitmap       = kernel_layout.bitmap_va;
    pmm_state.bitmap_bytes = bitmap_bytes;
    memset(pmm_state.bitmap, 0, bitmap_size);

    // Reserve boot-time regions
    pmm_reserve_boot_regions();
}

/* mark: mark pages in [start, end) as USED */
int pmm_mark_range(uintptr_t start, uintptr_t end) {
    if (start >= end) return MARK_FAIL;

    /* Align the range to page boundaries */
    uintptr_t astart = align_down(start, PAGE_SIZE);
    uintptr_t aend   = align_up(end, PAGE_SIZE);

    size_t start_pfn = astart / PAGE_SIZE;
    size_t end_pfn   = aend   / PAGE_SIZE;

    /* PFN bounds check (pfn_end is exclusive) */
    if (start_pfn < pmm_state.pfn_base || end_pfn > pmm_state.pfn_end) {
        return MARK_FAIL;
    }

    kassert(pmm_state.bitmap != NULL);
    kassert(pmm_state.pfn_end > pmm_state.pfn_base);
    kassert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    kassert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);

    for (size_t pfn = start_pfn; pfn < end_pfn; pfn++) {
        size_t index = pfn - pmm_state.pfn_base;
        size_t byte_idx = index / 8;
        size_t bit_idx  = index % 8;

        /* safety: ensure we do not walk past bitmap */
        kassert(byte_idx < pmm_state.bitmap_bytes);
        if (byte_idx >= pmm_state.bitmap_bytes) break;

        uint8_t mask = (uint8_t)(1u << bit_idx);

        /* Only flip and update counters if bit was previously 0 */
        if (!(pmm_state.bitmap[byte_idx] & mask)) {
            pmm_state.bitmap[byte_idx] |= mask;
            if (pmm_state.free_pages > 0) pmm_state.free_pages--;
            kassert(pmm_state.free_pages <= pmm_state.total_pages);
        }
    }

    return MARK_OK;
}

/* unmark: mark pages in [start, end) as FREE */
int pmm_unmark_range(uintptr_t start, uintptr_t end) {
    if (start >= end) return MARK_FAIL;

    uintptr_t astart = align_down(start, PAGE_SIZE);
    uintptr_t aend   = align_up(end, PAGE_SIZE);

    size_t start_pfn = astart / PAGE_SIZE;
    size_t end_pfn   = aend   / PAGE_SIZE;

    if (start_pfn < pmm_state.pfn_base || end_pfn > pmm_state.pfn_end) {
        return MARK_FAIL;
    }

    kassert(pmm_state.bitmap != NULL);
    kassert(pmm_state.pfn_end > pmm_state.pfn_base);
    kassert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    kassert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);

    for (size_t pfn = start_pfn; pfn < end_pfn; pfn++) {
        size_t index = pfn - pmm_state.pfn_base;
        size_t byte_idx = index / 8;
        size_t bit_idx  = index % 8;

        kassert(byte_idx < pmm_state.bitmap_bytes);
        if (byte_idx >= pmm_state.bitmap_bytes) break;

        uint8_t mask = (uint8_t)(1u << bit_idx);

        /* Only flip and update counters if bit was previously 1 */
        if (pmm_state.bitmap[byte_idx] & mask) {
            pmm_state.bitmap[byte_idx] &= ~mask;
            if (pmm_state.free_pages < pmm_state.total_pages) pmm_state.free_pages++;
            kassert(pmm_state.free_pages <= pmm_state.total_pages);
        }
    }

    return MARK_OK;
}

/* alloc_page: return physical address of one page or 0 on failure */
uintptr_t pmm_alloc_page(void) {
    if (pmm_state.free_pages == 0) return (uintptr_t)0;

    kassert(pmm_state.bitmap != NULL);
    kassert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    kassert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);
    kassert(pmm_state.free_pages <= pmm_state.total_pages);

    size_t total_pages = pmm_state.total_pages;

    for (size_t byte = 0; byte < pmm_state.bitmap_bytes; byte++) {
        uint8_t val = pmm_state.bitmap[byte];
        if (val == 0xFF) continue; /* all used */

        for (int bit = 0; bit < 8; bit++) {
            size_t index = byte * 8 + bit;
            if (index >= total_pages) break; /* beyond managed pages */

            uint8_t mask = (uint8_t)(1u << bit);

            if (!(val & mask)) { /* free */
                /* mark allocated */
                pmm_state.bitmap[byte] |= mask;
                pmm_state.free_pages--;
                kassert(pmm_state.free_pages <= pmm_state.total_pages);

                size_t pfn = pmm_state.pfn_base + index;
                uintptr_t addr = (uintptr_t)pfn * PAGE_SIZE;
                kassert(addr % PAGE_SIZE == 0);
                kassert(pfn >= pmm_state.pfn_base && pfn < pmm_state.pfn_end);
                return addr;
            }
        }
    }

    return (uintptr_t)0;
}
uintptr_t pmm_alloc_pages(size_t n_pages) {
    if (n_pages == 0 || pmm_state.free_pages < n_pages) return (uintptr_t)0;

    kassert(pmm_state.bitmap != NULL);
    kassert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    kassert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);
    kassert(pmm_state.free_pages <= pmm_state.total_pages);
    kassert(n_pages <= pmm_state.total_pages);

    size_t total_pages = pmm_state.total_pages;
    size_t consecutive = 0;
    size_t start_index = 0;

    for (size_t index = 0; index < total_pages; index++) {
        size_t byte_idx = index / 8;
        size_t bit_idx  = index % 8;
        uint8_t mask = (uint8_t)(1u << bit_idx);

        if (byte_idx >= pmm_state.bitmap_bytes) {
            break; /* beyond managed pages */
        }

        if (!(pmm_state.bitmap[byte_idx] & mask)) { /* free */
            if (consecutive == 0) {
                start_index = index;
            }
            consecutive++;

            if (consecutive == n_pages) {
                /* Mark pages as allocated */
                if (pmm_mark_range(start_index * PAGE_SIZE + pmm_state.pfn_base * PAGE_SIZE,
                     (start_index + n_pages) * PAGE_SIZE + pmm_state.pfn_base * PAGE_SIZE) != MARK_OK) {
                        return (uintptr_t)0; /* marking failed */
                }

                size_t pfn = pmm_state.pfn_base + start_index;
                uintptr_t addr = (uintptr_t)pfn * PAGE_SIZE;
                kassert(addr % PAGE_SIZE == 0);
                kassert(pfn >= pmm_state.pfn_base && (pfn + n_pages) <= pmm_state.pfn_end);
                return addr;
            }
        } else {
            consecutive = 0; /* reset */
        }
    }

    return (uintptr_t)0;
}

int pmm_free_page(uintptr_t addr) {
    if (addr % PAGE_SIZE != 0) return FREE_FAIL;

    size_t pfn = addr / PAGE_SIZE;

    /* bounds: pfn must be inside [pfn_base, pfn_end) */
    if (pfn < pmm_state.pfn_base || pfn >= pmm_state.pfn_end) {
        return FREE_FAIL;
    }

    size_t index = pfn - pmm_state.pfn_base;
    size_t byte_idx = index / 8;
    size_t bit_idx  = index % 8;

    if (byte_idx >= pmm_state.bitmap_bytes) return FREE_FAIL;

    kassert(pmm_state.bitmap != NULL);
    kassert(pmm_state.free_pages <= pmm_state.total_pages);

    uint8_t mask = (uint8_t)(1u << bit_idx);

    /* if bit set -> allocated -> free it */
    if (pmm_state.bitmap[byte_idx] & mask) {
        pmm_state.bitmap[byte_idx] &= ~mask;
        pmm_state.free_pages++;
        kassert(pmm_state.free_pages <= pmm_state.total_pages);
        return FREE_OK;
    }

    /* already free -> double free */
    return DOUBLE_FREE;
}

uintptr_t pmm_alloc_pages_aligned(size_t n_pages, size_t align_pages)
{
    if (n_pages == 0) return (uintptr_t)0;
    if (align_pages == 0) align_pages = 1;

    // Require power-of-two alignment (common + cheap)
    if ((align_pages & (align_pages - 1)) != 0) return (uintptr_t)0;

    if (pmm_state.free_pages < n_pages) return (uintptr_t)0;

    kassert(pmm_state.bitmap != NULL);
    kassert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    kassert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);

    size_t total_pages = pmm_state.total_pages;
    size_t consecutive = 0;
    size_t start_index = 0;

    for (size_t index = 0; index < total_pages; index++) {

        // Enforce alignment on the start of a run
        if (consecutive == 0) {
            if ((index & (align_pages - 1)) != 0) {
                continue;
            }
        }

        size_t byte_idx = index / 8;
        size_t bit_idx  = index % 8;
        uint8_t mask = (uint8_t)(1u << bit_idx);

        if (byte_idx >= pmm_state.bitmap_bytes) break;

        if (!(pmm_state.bitmap[byte_idx] & mask)) { // free
            if (consecutive == 0) start_index = index;
            consecutive++;

            if (consecutive == n_pages) {
                uintptr_t start_pa = (uintptr_t)(pmm_state.pfn_base + start_index) * PAGE_SIZE;
                uintptr_t end_pa   = (uintptr_t)(pmm_state.pfn_base + start_index + n_pages) * PAGE_SIZE;

                if (pmm_mark_range(start_pa, end_pa) != MARK_OK) {
                    return (uintptr_t)0;
                }

                return start_pa;
            }
        } else {
            consecutive = 0;
        }
    }

    return (uintptr_t)0;
}