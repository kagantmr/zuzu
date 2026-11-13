/**
 * @file vmm_boot.c
 * @brief Kernel address space bootstrap, identity mapping removal, and kernel image lockdown.
 */

#include "vmm_internal.h"

#include "core/panic.h"
#include "kernel/layout.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm/pmm.h"

#include <arch/mmu.h>
#include <arch/symbols.h>
#include <string.h>

/** @brief The kernel's physical memory layout, filled in during early boot. */
extern RamLayout kernel_layout;

#define LOG_FMT(fmt) "(vmm) " fmt
#include "core/log.h"

/**
 * @brief Maps [@p start, @p end) of RAM at its linear-map address, or panics.
 *
 * @param[in] start Physical start; page-aligned.
 * @param[in] end   Physical end, exclusive; page-aligned.
 * @param[in] pages Whether to use pages only, so that parts of the range can be protected later.
 */
static void MapLinear(PhysAddr start, PhysAddr end, bool pages)
{
    if (start >= end)
        return;

    size_t size = end - start;
    MemProt prot = PROT_READ | PROT_WRITE | PROT_EXEC;
    bool ok = pages ? ArchMmuMapPages(g_kernel_as, PA_TO_VA(start), start, size, prot, VM_MEM_NORMAL)
                    : ArchMmuMap(g_kernel_as, PA_TO_VA(start), start, size, prot, VM_MEM_NORMAL);
    if (!ok)
        panic("Failed to map RAM [%p..%p)", (void *)start, (void *)end);
}

void VmmBootstrap(void)
{
    if (g_kernel_as)
        return;

    g_kernel_as = KZAlloc(sizeof(AddressSpace));
    if (!g_kernel_as)
        panic("Failed to create kernel address space");

    g_kernel_as->type = ADDRESS_SPACE_KERNEL;
    g_kernel_as->asid_token = (AsidToken){0};
    g_kernel_as->pt_root_physaddr = ArchMmuCreateTables(ADDRESS_SPACE_KERNEL);
    if (!g_kernel_as->pt_root_physaddr)
        panic("Failed to allocate kernel page tables");
    if (!vm_region_vec_init(&g_kernel_as->regions))
        panic("Failed to allocate kernel region list");

    PhysAddr image_start = align_down(kernel_layout.kernel_start_pa, PAGE_SIZE);
    PhysAddr image_end = align_up(kernel_layout.kernel_end_pa, PAGE_SIZE);

    for (size_t i = 0; i < kernel_layout.ram_bank_count; i++) {
        PhysAddr bank_start = kernel_layout.ram_banks[i].start;
        PhysAddr bank_end = kernel_layout.ram_banks[i].end;

        if (image_start >= bank_start && image_end <= bank_end) {
            MapLinear(bank_start, image_start, false);
            MapLinear(image_start, image_end, true);
            MapLinear(image_end, bank_end, false);
        } else {
            MapLinear(bank_start, bank_end, false);
        }

        VirtMemRegion ram_region = {
            .vaddr_start = PA_TO_VA(bank_start),
            .size = bank_end - bank_start,
            .prot = PROT_READ | PROT_WRITE | PROT_EXEC,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_BACKING_SHARED,
            .flags = VM_FLAG_PINNED,
        };
        VmmAddRegion(g_kernel_as, &ram_region);
        KDEBUG("mapped ram bank [%p..%p)", (void *)bank_start, (void *)bank_end);
    }

    if (!ArchMmuInstallKernelSpace(g_kernel_as))
        panic("Failed to install the kernel address space");

    g_mmu_enabled = true;
    g_current_addrspace = g_kernel_as;
    KDEBUG("kernel space installed, ram [%p..%p)", (void *)kernel_layout.ram_start,
           (void *)kernel_layout.ram_end);
}

void VmmRemoveIdentityMapping(void)
{
    if (!g_kernel_as)
        return;

    ArchMmuDropIdentity(g_kernel_as);
    KDEBUG("identity mapping removed, running pure higher-half");
}

void VmmLockdownKernelMapping(void)
{
    VirtAddr text = (VirtAddr)_kernel_start;
    VirtAddr rodata = (VirtAddr)_rodata_start;
    VirtAddr data = (VirtAddr)_data_start;

    ArchMmuProtect(g_kernel_as, text, rodata - text, PROT_READ | PROT_EXEC);
    ArchMmuProtect(g_kernel_as, rodata, data - rodata, PROT_READ);
}
