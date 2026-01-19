#include "kernel/mm/pmm.h"
#include "core/assert.h"

pmm_state_t pmm_state;

static inline uintptr_t align_down(uintptr_t x, uintptr_t a) {
    kassert(a != 0);
    return x & ~(a - 1);
}
static inline uintptr_t align_up(uintptr_t x, uintptr_t a)   {
    kassert(a != 0);
    return (x + a - 1) & ~(a - 1);
}

/* mark: mark pages in [start, end) as USED */
int pmm_mark_phys_page(uintptr_t start, uintptr_t end) {
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
int pmm_unmark_phys_page(uintptr_t start, uintptr_t end) {
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
                if (pmm_mark_phys_page(start_index * PAGE_SIZE + pmm_state.pfn_base * PAGE_SIZE,
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