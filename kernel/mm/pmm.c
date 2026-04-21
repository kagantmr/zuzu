#include "kernel/mm/pmm.h"
#include <assert.h>
#include "kernel/layout.h"
#include "arch/arm/include/symbols.h"
#include <mem.h>
#include "kernel/mm/vmm.h"   // PA_TO_VA / VA_TO_PA helpers

#define LOG_FMT(fmt) "(pmm) " fmt
#include "core/log.h"

pmm_state_t pmm_state;
extern phys_region_t phys_region;
extern kernel_layout_t kernel_layout;
extern void syspage_update_mem(void);

static bool pmm_is_valid_managed_pa(uintptr_t pa) {
    if (pa == 0) return true; // freelist terminator
    if ((pa % PAGE_SIZE) != 0) return false;

    const size_t pfn = pa / PAGE_SIZE;
    return (pfn >= pmm_state.pfn_base && pfn < pmm_state.pfn_end);
}

static void pmm_rebuild_freelist(void) {
    pmm_state.freelist_head = 0;
    for (size_t i = 0; i < pmm_state.total_pages; i++) {
        size_t byte_idx = i / 8;
        size_t bit_idx  = i % 8;
        if (!(pmm_state.bitmap[byte_idx] & (1u << bit_idx))) {
            uintptr_t pa = (pmm_state.pfn_base + i) * PAGE_SIZE;
            uintptr_t *page_va = (uintptr_t *)PA_TO_VA(pa);
            *page_va = pmm_state.freelist_head;
            pmm_state.freelist_head = pa;
        }
    }
}

/* Remove any free-list nodes whose PA is within [start_pa, end_pa). */
static void pmm_freelist_remove_range(uintptr_t start_pa, uintptr_t end_pa) {
    uintptr_t prev_pa = 0;
    uintptr_t curr_pa = pmm_state.freelist_head;

    while (curr_pa) {
        if (!pmm_is_valid_managed_pa(curr_pa)) {
            pmm_rebuild_freelist();
            prev_pa = 0;
            curr_pa = pmm_state.freelist_head;
            continue;
        }

        uintptr_t *curr_va = (uintptr_t *)PA_TO_VA(curr_pa);
        uintptr_t next_pa = *curr_va;

        if (!pmm_is_valid_managed_pa(next_pa)) {
            pmm_rebuild_freelist();
            prev_pa = 0;
            curr_pa = pmm_state.freelist_head;
            continue;
        }

        if (curr_pa >= start_pa && curr_pa < end_pa) {
            if (prev_pa == 0) {
                pmm_state.freelist_head = next_pa;
            } else {
                uintptr_t *prev_va = (uintptr_t *)PA_TO_VA(prev_pa);
                *prev_va = next_pa;
            }
        } else {
            prev_pa = curr_pa;
        }

        curr_pa = next_pa;
    }
}

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

    // Build the freelist from all free pages in the bitmap
    pmm_rebuild_freelist();
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

    const uintptr_t astart = align_down(start, PAGE_SIZE);
    const uintptr_t aend   = align_up(end, PAGE_SIZE);

    const size_t start_pfn = astart / PAGE_SIZE;
    size_t end_pfn   = aend   / PAGE_SIZE;

    if (start_pfn < pmm_state.pfn_base || end_pfn > pmm_state.pfn_end) {
        return MARK_FAIL;
    }

    kassert(pmm_state.bitmap != NULL);
    kassert(pmm_state.pfn_end > pmm_state.pfn_base);
    kassert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    kassert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);

    for (size_t pfn = start_pfn; pfn < end_pfn; pfn++) {
        const size_t index = pfn - pmm_state.pfn_base;
        const size_t byte_idx = index / 8;
        const size_t bit_idx  = index % 8;

        kassert(byte_idx < pmm_state.bitmap_bytes);
        if (byte_idx >= pmm_state.bitmap_bytes) break;

        const uint8_t mask = (uint8_t)(1u << bit_idx);

        /* Only flip and update counters if bit was previously 1 */
        if (pmm_state.bitmap[byte_idx] & mask) {
            pmm_state.bitmap[byte_idx] &= ~mask;
            if (pmm_state.free_pages < pmm_state.total_pages) pmm_state.free_pages++;
            kassert(pmm_state.free_pages <= pmm_state.total_pages);
        }
    }

    return MARK_OK;
}

/* alloc_page: pop one page from freelist, O(1) */
uintptr_t pmm_alloc_page(void) {
    if (pmm_state.freelist_head == 0) return (uintptr_t)0;

    if (!pmm_is_valid_managed_pa(pmm_state.freelist_head)) {
        pmm_rebuild_freelist();
        if (pmm_state.freelist_head == 0) return (uintptr_t)0;
    }

    uintptr_t pa = pmm_state.freelist_head;

    /* Pop: read next pointer stored in the page itself */
    uintptr_t *page_va = (uintptr_t *)PA_TO_VA(pa);
    uintptr_t next_pa = *page_va;
    if (!pmm_is_valid_managed_pa(next_pa)) {
        pmm_rebuild_freelist();
        if (pmm_state.freelist_head == 0) return (uintptr_t)0;
        pa = pmm_state.freelist_head;
        page_va = (uintptr_t *)PA_TO_VA(pa);
        next_pa = *page_va;
        if (!pmm_is_valid_managed_pa(next_pa)) {

            pmm_state.freelist_head = 0;
            return (uintptr_t)0;
        }
    }
    pmm_state.freelist_head = next_pa;

    /* Keep bitmap in sync */
    size_t index = (pa / PAGE_SIZE) - pmm_state.pfn_base;
    size_t byte_idx = index / 8;
    size_t bit_idx  = index % 8;

    kassert(byte_idx < pmm_state.bitmap_bytes);
    kassert(!(pmm_state.bitmap[byte_idx] & (1u << bit_idx))); /* must be free in bitmap */

    pmm_state.bitmap[byte_idx] |= (uint8_t)(1u << bit_idx);
    pmm_state.free_pages--;
    kassert(pmm_state.free_pages <= pmm_state.total_pages);

    syspage_update_mem();
    return pa;
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
                uintptr_t start_pa = start_index * PAGE_SIZE + pmm_state.pfn_base * PAGE_SIZE;
                uintptr_t end_pa = (start_index + n_pages) * PAGE_SIZE + pmm_state.pfn_base * PAGE_SIZE;
                if (pmm_mark_range(start_pa, end_pa) != MARK_OK) {
                        return (uintptr_t)0; /* marking failed */
                }

                /* Keep freelist in sync without a full O(total_pages) rebuild. */
                pmm_freelist_remove_range(start_pa, end_pa);

                size_t pfn = pmm_state.pfn_base + start_index;
                uintptr_t addr = (uintptr_t)pfn * PAGE_SIZE;
                kassert(addr % PAGE_SIZE == 0);
                kassert(pfn >= pmm_state.pfn_base && (pfn + n_pages) <= pmm_state.pfn_end);
                syspage_update_mem(); // update free memory info in syspage
                return addr;
            }
        } else {
            consecutive = 0; /* reset */
        }
    }

    return (uintptr_t)0;
}

int pmm_free_page(const uintptr_t addr) {
    if (addr % PAGE_SIZE != 0) return FREE_FAIL;

    const size_t pfn = addr / PAGE_SIZE;

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

    const uint8_t mask = (uint8_t)(1u << bit_idx);

    /* if bit set -> allocated -> free it */
    if (pmm_state.bitmap[byte_idx] & mask) {
        pmm_state.bitmap[byte_idx] &= ~mask;
        pmm_state.free_pages++;
        kassert(pmm_state.free_pages <= pmm_state.total_pages);

        /* Push onto freelist */
        uintptr_t *page_va = (uintptr_t *)PA_TO_VA(addr);
        *page_va = pmm_state.freelist_head;
        pmm_state.freelist_head = addr;

        syspage_update_mem();
        return FREE_OK;
    }

    /* already free -> double free */
    return DOUBLE_FREE;
}

uintptr_t pmm_alloc_pages_aligned(const size_t n_pages, size_t align_pages)
{
    if (n_pages == 0) return (uintptr_t)0;
    if (align_pages == 0) align_pages = 1;

    // Require power-of-two alignment (common + cheap)
    if ((align_pages & (align_pages - 1)) != 0) return (uintptr_t)0;

    if (pmm_state.free_pages < n_pages) return (uintptr_t)0;

    kassert(pmm_state.bitmap != NULL);
    kassert(pmm_state.total_pages == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    kassert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_pages);

    const size_t total_pages = pmm_state.total_pages;
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
                const uintptr_t start_pa = (uintptr_t)(pmm_state.pfn_base + start_index) * PAGE_SIZE;
                const uintptr_t end_pa   = (uintptr_t)(pmm_state.pfn_base + start_index + n_pages) * PAGE_SIZE;

                if (pmm_mark_range(start_pa, end_pa) != MARK_OK) {
                    return (uintptr_t)0;
                }

                /* Keep freelist in sync without a full O(total_pages) rebuild. */
                pmm_freelist_remove_range(start_pa, end_pa);
                
                syspage_update_mem(); // update free memory info in syspage
                return start_pa;
            }
        } else {
            consecutive = 0;
        }
    }

    return (uintptr_t)0;
}

size_t pmm_alloc_pages_scattered(const size_t n_pages, uintptr_t *out_addrs) {
    if (!n_pages || !out_addrs) {
        return 0;
    }
    for (size_t i = 0; i < n_pages; i++) {
        const uintptr_t new_page = pmm_alloc_page();
        if (new_page == 0) return i;
        out_addrs[i] = new_page;
    }
    syspage_update_mem(); // update free memory info in syspage
    return n_pages;
}