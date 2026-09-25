/* Kernel-VA window for MMIO device mappings: a section-granularity
 * allocator over [IOREMAP_BASE, IOREMAP_MAX_SLOT) plus the small table
 * that tracks each live mapping so IoUnmap() can find it again. */

#include "vmm_internal.h"

#include <arch/mmu.h>
#include <bitmap.h>
#include <stdint.h>

// Bitmap: 256 bits = 8 x uint32_t
static uint32_t ioremap_bitmap[8];  // Bit N = slot N allocated

/* IOREMAP_MAX_SLOT is defined in vmm.h as pure text substitution (SECTION_SIZE
 * isn't visible there yet, see the comment at its definition); this is the
 * first point in this TU where both arch/mmu.h (SECTION_SIZE) and vmm.h
 * (KSTACK_REGION_BASE, IOREMAP_BASE, IOREMAP_SLOTS) are in scope together. */
_Static_assert(IOREMAP_MAX_SLOT <= IOREMAP_SLOTS,
	       "kstack region base falls outside the ioremap window");

typedef struct {
    VirtAddr va;       // Base VA (0 = unused entry)
    PhysAddr pa;       // Physical address
    uint32_t sections;  // Number of 1MB sections
} IoremapEntry;

static IoremapEntry ioremap_table[IOREMAP_MAX_ENTRIES];

// Find N contiguous free slots. Bounded to IOREMAP_MAX_SLOT, not
// IOREMAP_SLOTS: slots beyond that would land on the kstack region
// (see IOREMAP_MAX_SLOT in vmm.h).
static int BitmapFindFree(uint32_t n) {
    return BitmapFindClearRun(ioremap_bitmap, IOREMAP_MAX_SLOT, n);
}

static void BitmapAlloc(uint32_t start, uint32_t count) {
    BitmapSetRange(ioremap_bitmap, start, count);
}

static void BitmapFree(uint32_t start, uint32_t count) {
    BitmapClrRange(ioremap_bitmap, start, count);
}

// Find ioremap_table entry by VA
static IoremapEntry *IoremapFind(VirtAddr va) {
    for (size_t i = 0; i < IOREMAP_MAX_ENTRIES; i++) {
        if (ioremap_table[i].va == va) {
            return &ioremap_table[i];
        }
    }
    return NULL;
}

// Find free slot in ioremap_table
static IoremapEntry *IoremapAllocEntry(void) {
    for (size_t i = 0; i < IOREMAP_MAX_ENTRIES; i++) {
        if (ioremap_table[i].va == 0) {
            return &ioremap_table[i];
        }
    }
    return NULL;
}

void *IoRemap(PhysAddr phys, size_t size) {
    if (size == 0) {
        return NULL;
    }

    uintptr_t phys_aligned = align_down(phys, SECTION_SIZE);
    uintptr_t offset = phys - phys_aligned;
    size_t total_size = size + offset;
    size_t aligned_size = align_up(total_size, SECTION_SIZE);
    uint32_t sections_needed = aligned_size / SECTION_SIZE;

    int slot = BitmapFindFree(sections_needed);
    if (slot < 0) {
        return NULL;
    }
    /* Belt-and-suspenders: BitmapFindFree is already bounded to
     * IOREMAP_MAX_SLOT, but a mapping must never be allowed to land on the
     * kstack region even if that bound is ever loosened by mistake. */
    if ((uint32_t)slot + sections_needed > IOREMAP_MAX_SLOT) {
        return NULL;
    }

    uintptr_t va = IOREMAP_BASE + ((uint32_t)slot * SECTION_SIZE);

    if (!VmmMapRange(g_kernel_as, va, phys_aligned, aligned_size,
                       PROT_READ | PROT_WRITE,
                       VM_MEM_DEVICE, VM_OWNER_NONE,
                       VM_FLAG_PINNED | VM_FLAG_GLOBAL)) {
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
    entry->pa = phys_aligned;
    entry->sections = sections_needed;

    return (void *)(va + offset);
}

void IoUnmap(void *va) {
    if (!va) {
        return;
    }

    uintptr_t base_va = align_down((uintptr_t)va, SECTION_SIZE);
    IoremapEntry *entry = IoremapFind(base_va);
    if (!entry) {
        return;
    }

    size_t size = entry->sections * SECTION_SIZE;
    VmmUnmapRange(g_kernel_as, entry->va, size, true);

    uint32_t slot_start = (entry->va - IOREMAP_BASE) / SECTION_SIZE;
    BitmapFree(slot_start, entry->sections);

    entry->va = 0;
    entry->pa = 0;
    entry->sections = 0;
}
