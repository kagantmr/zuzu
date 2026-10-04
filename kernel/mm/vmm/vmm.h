#ifndef KERNEL_MM_VMM_VMM_H
#define KERNEL_MM_VMM_VMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <vector.h>
#include BOARD_LAYOUT_H
#include <arch/asid.h>

#define PA_TO_VA(pa) ((VirtAddr)(pa) + KERNEL_VA_OFFSET)
#define VA_TO_PA(va) ((PhysAddr)(va) - KERNEL_VA_OFFSET)

typedef struct SpaceObjectStruct SpaceObject;
typedef struct HandleTableEntryStruct HandleTableEntry;

#define IOREMAP_MAX_ENTRIES 16

#define VM_PROT_USER (1U << 3) // user-accessible (otherwise kernel-only)

typedef enum {
    VM_MEM_NORMAL = 0,
    VM_MEM_DEVICE = 1,
} VirtMemType;

typedef enum {
    VM_BACKING_NONE = 0,   // Physical pages NOT owned by this addrspace.
                           // Used for MMIO, device memory, external allocations.
                           // On destroy: unmap only, do NOT free pages.
    VM_BACKING_ANON = 1,   // Physical pages allocated by PMM for this addrspace.
                           // Used for anonymous memory (heap, stack, user allocations).
                           // On destroy: must walk page tables, translate VA→PA, free pages to PMM.
    VM_BACKING_SHARED = 2, // Physical pages owned by a different addrspace or subsystem.
                           // Used for shared kernel mappings, copy-on-write, etc.
                           // On destroy: unmap only, do NOT free pages.
} VirtMemBacking;

typedef enum {
    VM_FLAG_NONE = 0,
    VM_FLAG_PINNED = 1U << 0, // must stay mapped
    VM_FLAG_GUARD = 1U << 2,  // guard page/region
} VirtMemFlags;

typedef struct VirtMemRegionStruct {
    VirtAddr vaddr_start;
    size_t size;
    MemProt prot;
    VirtMemType memtype;
    VirtMemBacking owner; // ownership: who allocated/owns the backing pages
    VirtMemFlags flags;
    void *backing; // optional backing MemObject for shared and device mappings
} VirtMemRegion;

typedef enum {
    ADDRESS_SPACE_KERNEL = 0,
    ADDRESS_SPACE_USER = 1,
} AddressSpaceType;

DEFINE_VEC(vm_region, VirtMemRegion)

typedef struct AddressSpaceStruct {
    PhysAddr pt_root_physaddr; // physical address of level-1 table
    vm_region_vec_t regions;
    AddressSpaceType type;
    AsidToken asid_token;
} AddressSpace;

#define IOREMAP_SIZE (IOREMAP_END - IOREMAP_BASE + 1)
#define IOREMAP_SLOTS (IOREMAP_SIZE / SECTION_SIZE) // 256

/* ioremap and the kernel-stack pool share the same [IOREMAP_BASE, IOREMAP_END]
 * window: ioremap grows up from IOREMAP_BASE in 1MB sections, kstack owns
 * [KSTACK_REGION_BASE, KSTACK_REGION_TOP). Slots at/after this bound would
 * place a device mapping on top of live kernel stacks, so ioremap must never
 * hand one out.
 * SECTION_SIZE comes from arch/mmu.h, which itself includes this header, so
 * it isn't visible yet at this point in the first (defining) pass over this
 * file — this macro is fine as pure text substitution (same as IOREMAP_SLOTS
 * above), but a _Static_assert here would evaluate too early. See ioremap.c
 * for the assert once both headers are actually in scope. */
#define IOREMAP_MAX_SLOT ((KSTACK_REGION_BASE - IOREMAP_BASE) / SECTION_SIZE)

AddressSpace *VmmGetKernelAddressSpace(void);

/**
 * @brief Create a new address space.
 * @param type ADDRESS_SPACE_KERNEL or ADDRESS_SPACE_USER.
 * @return Pointer to the newly created address space, or NULL on failure.
 */
AddressSpace *AddressSpaceCreate(AddressSpaceType type);

/**
 * @brief Destroy an address space.
 * @param as Address space to destroy.
 * Responsibilities: unmap regions, free page tables, release physical memory.
 */
void AddressSpaceDestroy(AddressSpace *as);

/**
 * @brief Add a region to an address space.
 * @param as Address space.
 * @param region Region to add (vaddr_start, size, prot, memtype, flags).
 * @return true on success, false if overlap or alignment error.
 * Does not touch page tables; only validates and appends to as->regions.
 */
/* region is always a stack/global compound literal, never an alias of
 * anything reachable through as (in particular never a pointer into
 * as->regions.data itself). */
bool VmmAddRegion(AddressSpace *restrict as, const VirtMemRegion *restrict region);

/**
 * @brief Remove a region from an address space.
 * @param as Address space.
 * @param vaddr Virtual address of the region to remove.
 * @param size Size of the region.
 * @return true on success, false if not found.
 */
bool VmmRemoveRegion(AddressSpace *as, VirtAddr vaddr, size_t size);

/**
 * @brief First-fit free virtual range within [lo, hi).
 * @param as Address space whose region list defines what is occupied.
 * @param lo Arena start (inclusive).
 * @param hi Arena end (exclusive).
 * @param size Bytes required; must be non-zero.
 * @return Base of a free range, or 0 if the arena cannot hold it.
 */
VirtAddr VmmFindFreeVa(const AddressSpace *as, VirtAddr lo, VirtAddr hi, size_t size);

/**
 * @brief Bootstrap the virtual memory system (early boot).
 * Responsibilities:
 *   1. create kernel address space
 *   2. add required kernel regions
 *   3. build page tables
 *   4. enable MMU via arch layer
 *   5. handle post-MMU transition
 * Called once during early boot.
 */
void VmmBootstrap(void);

/**
 * @brief Activate/switch to an address space.
 * @param as Address space to activate.
 * Calls arch layer to load TTBR0 and flush TLB.
 * Used for context switching and userspace entry.
 */
void VmmActivateAddressSpace(AddressSpace *as);

/**
 * @brief High-level mapping API: add a single mapping to an address space.
 * @param as Address space.
 * @param va Virtual address (should be page-aligned).
 * @param pa Physical address (should be page-aligned).
 * @param size Size in bytes (should be page-aligned or section-aligned).
 * @param prot Protection flags (PROT_READ | PROT_WRITE | VM_PROT_USER | ...).
 * @param memtype VM_MEM_NORMAL or VM_MEM_DEVICE.
 * @return true on success, false on error.
 */
bool VmmMapRange(AddressSpace *as, VirtAddr va, PhysAddr pa, size_t size, MemProt prot,
                 VirtMemType memtype);

/**
 * @brief Remove mappings from an address space.
 * @param as Address space.
 * @param va Virtual address.
 * @param size Size in bytes.
 * @return true on success, false if not found.
 *
 * Responsibilities (what this function does):
 *   - Clear page table entries for the VA range
 *   - Invalidate TLB entries (via ArchMmuUnmap)
 *   - Optionally free empty L2 tables (architecture-dependent)
 *
 * What this function does NOT do:
 *   - Does NOT free physical pages back to PMM
 *   - Does NOT unmark pages in PMM
 *
 * Contract: The caller is responsible for freeing physical pages.
 * If the region owns its pages (VM_BACKING_ANON), the caller must walk
 * page tables BEFORE unmapping to discover which PAs to free.
 *
 * TLB Handling: VmmUnmapRange calls ArchMmuUnmap, which handles
 * TLB invalidation. If the addrspace is active (TTBR0), the TLB must
 * be invalidated for this to take effect.
 */
bool VmmUnmapRange(AddressSpace *as, VirtAddr va, size_t size, bool flush);

/**
 * @brief Change permissions on a range.
 * @param as Address space.
 * @param va Virtual address.
 * @param size Size in bytes.
 * @param new_prot New protection flags.
 * @return true on success, false if not found.
 */
bool VmmProtectPage(AddressSpace *as, VirtAddr va, size_t size, MemProt new_prot);

/**
 * @brief Map a page into user address space.
 * @param as User address space.
 * @param pa Physical address of the page.
 * @param va Virtual address in user space.
 * @param prot Protection flags.
 * @return true on success, false on error.
 */
bool VmmMapUserPage(AddressSpace *as, PhysAddr pa, VirtAddr va, MemProt prot);

Err VmmMapAnon(SpaceObject *space, VirtAddr hint, size_t size, MemProt prot, VirtAddr *out);

Err VmmMapMemObj(SpaceObject *space, HandleTableEntry *entry, MemProt prot, VirtAddr hint,
                 VirtAddr *out);

Err VmmUnmapUserRegion(SpaceObject *space, VirtAddr va);

Err InjectIntoSpace(SpaceObject *kitten, SpaceObject *parent, InjectArgs *args);

Err VmmProtectUserRange(SpaceObject *space, VirtAddr va, size_t size, MemProt new_prot);

/**
 * @brief Remove the identity mapping from the kernel address space.
 * After calling this, the kernel runs purely in higher-half memory.
 */
void VmmRemoveIdentityMapping(void);

/**
 * @brief Map a physical address range into kernel virtual address space for I/O.
 * @param phys Physical address to map.
 * @param size Size of the mapping.
 * @return Virtual address of the mapped region, or NULL on failure.
 */
void *IoRemap(PhysAddr phys, size_t size);

/**
 * @brief Unmap a previously mapped I/O region.
 * @param va Virtual address returned by ioremap.
 */
void IoUnmap(void *va);

/**
 * @brief Check if `[va, va+len]` being accessed in `as` would cause a data abort
 *
 * @param va The base addr of the region  to check.
 * @param len The size of ther region to check
 * @param as The address space
 * @param write R/W toggle.
 */
bool VmmCheckUserFault(AddressSpace *as, VirtAddr va, size_t len, bool write);

/* region always points into as->regions.data, but the AddressSpace bytes
 * themselves (pt_root_physaddr, the regions vector header) and the
 * VirtMemRegion bytes region points at never overlap -- on the lazy-mapping
 * hot path (ServiceDemandPage -> here), this is what makes it safe. */
bool VmmPageFaultHandle(AddressSpace *restrict as, VirtMemRegion *restrict region,
                        uintptr_t page_va);

void VmmLockdownKernelMapping(void);

#endif // KERNEL_MM_VMM_VMM_H
