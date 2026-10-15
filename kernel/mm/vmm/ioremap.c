#include "vmm_internal.h"

#include <arch/mmu.h>
#include <bitmap.h>
#include <stdint.h>

static uint32_t ioremap_bitmap[8];

_Static_assert(IOREMAP_MAX_SLOT <= IOREMAP_SLOTS,
               "kstack region base falls outside the ioremap window");

typedef struct {
    VirtAddr va;
    uint32_t sections;
} IoremapEntry;

static IoremapEntry ioremap_table[IOREMAP_MAX_ENTRIES];

static int BitmapFindFree(uint32_t n)
{
    return BitmapFindClearRun(ioremap_bitmap, IOREMAP_MAX_SLOT, n);
}

static void BitmapAlloc(uint32_t start, uint32_t count)
{
    BitmapSetRange(ioremap_bitmap, start, count);
}

static void BitmapFree(uint32_t start, uint32_t count)
{
    BitmapClrRange(ioremap_bitmap, start, count);
}

static IoremapEntry *IoremapFind(VirtAddr va)
{
    for (size_t i = 0; i < IOREMAP_MAX_ENTRIES; i++) {
        if (ioremap_table[i].va == va) {
            return &ioremap_table[i];
        }
    }
    return NULL;
}

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

    uintptr_t phys_aligned = align_down(phys, SECTION_SIZE);
    uintptr_t offset = phys - phys_aligned;
    size_t total_size = size + offset;
    size_t aligned_size = align_up(total_size, SECTION_SIZE);
    uint32_t sections_needed = aligned_size / SECTION_SIZE;

    int slot = BitmapFindFree(sections_needed);
    if (slot < 0) {
        return NULL;
    }
    
    if ((uint32_t)slot + sections_needed > IOREMAP_MAX_SLOT) {
        return NULL;
    }

    uintptr_t va = IOREMAP_BASE + ((uint32_t)slot * SECTION_SIZE);

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
    entry->sections = sections_needed;

    return (void *)(va + offset);
}

void IoUnmap(void *va)
{
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
    entry->sections = 0;
}
