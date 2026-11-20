/**
 * @file ioremap.c
 * @brief Section-granular mapping of device memory into the kernel ioremap window.
 */

#include "vmm_internal.h"

#include <arch/mmu.h>
#include <bitmap.h>
#include <stdint.h>

/** @brief Occupancy bitmap of the ioremap window, one bit per section slot. */
static BitmapWord ioremap_bitmap[BITMAP_WORDS(IOREMAP_MAX_SLOT)];

_Static_assert(IOREMAP_MAX_SLOT <= IOREMAP_SLOTS,
               "kstack region base falls outside the ioremap window");

/** @brief Bookkeeping for one live ioremap mapping. */
typedef struct {
    VirtAddr va;       /**< Section-aligned start of the mapping; 0 marks a free entry. */
} IoremapEntry;

/** @brief Table of live ioremap mappings. */
static IoremapEntry ioremap_table[IOREMAP_MAX_ENTRIES];

/**
 * @brief Find a run of free slots in the ioremap window.
 *
 * @param[in] n Number of consecutive slots needed.
 * @return Index of the first slot of the run, or a negative value if none exists.
 */
static int BitmapFindFree(uint32_t n)
{
    size_t slot = BitmapFindClearRun(ioremap_bitmap, IOREMAP_MAX_SLOT, n, 1, 0);
    return slot < IOREMAP_MAX_SLOT ? (int)slot : -1;
}

/**
 * @brief Mark a run of ioremap slots as used.
 *
 * @param[in] start First slot.
 * @param[in] count Number of slots.
 */
static void BitmapAlloc(uint32_t start, uint32_t count)
{
    BitmapSetRange(ioremap_bitmap, start, count);
}

/**
 * @brief Mark a run of ioremap slots as free.
 *
 * @param[in] start First slot.
 * @param[in] count Number of slots.
 */
static void BitmapFree(uint32_t start, uint32_t count)
{
    BitmapClrRange(ioremap_bitmap, start, count);
}

/**
 * @brief Find an unused table entry.
 *
 * @return A free entry, or NULL if the table is full.
 */
static IoremapEntry *IoremapAllocEntry(void)
{
    for (size_t i = 0; i < IOREMAP_MAX_ENTRIES; i++) {
        if (ioremap_table[i].va == 0) {
            return &ioremap_table[i];
        }
    }
    return NULL;
}

void *IoRemap(PhysAddr phys, size_t size)
{
    if (size == 0) {
        return NULL;
    }

    PhysAddr phys_aligned = align_down(phys, SECTION_SIZE);
    size_t offset = phys - phys_aligned;
    size_t total_size = size + offset;
    size_t aligned_size = align_up(total_size, SECTION_SIZE);
    uint32_t sections_needed = (uint32_t)(aligned_size / SECTION_SIZE);

    int slot = BitmapFindFree(sections_needed);
    if (slot < 0) {
        return NULL;
    }

    VirtAddr va = IOREMAP_BASE + ((uint32_t)slot * SECTION_SIZE);

    if (!VmmMapRange(g_kernel_as, va, phys_aligned, aligned_size, PROT_READ | PROT_WRITE,
                     VM_MEM_DEVICE)) {
        return NULL;
    }

    BitmapAlloc((uint32_t)slot, sections_needed);

    IoremapEntry *entry = IoremapAllocEntry();
    if (!entry) {
        VmmUnmapRange(g_kernel_as, va, aligned_size, true);
        BitmapFree((uint32_t)slot, sections_needed);
        return NULL;
    }
    entry->va = va;

    return (void *)(va + offset);
}

