#ifndef KERNEL_VM_VMM_H
#define KERNEL_VM_VMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SECTION_SIZE      0x100000UL     /* 1MB section for ARMv7 */

#define KERNEL_PA_BASE  0x80000000UL
#define KERNEL_VA_BASE  0xC0000000UL
#define KERNEL_VA_OFFSET (KERNEL_VA_BASE - KERNEL_PA_BASE)  // 0x40000000

// Convert between physical and virtual addresses for kernel memory
#define PA_TO_VA(pa)  ((uintptr_t)(pa) + KERNEL_VA_OFFSET)
#define VA_TO_PA(va)  ((uintptr_t)(va) - KERNEL_VA_OFFSET)



typedef enum {
    VM_PROT_NONE  = 0,
    VM_PROT_READ  = 1u << 0,
    VM_PROT_WRITE = 1u << 1,
    VM_PROT_EXEC  = 1u << 2,
    VM_PROT_USER  = 1u << 3, // user-accessible (otherwise kernel-only)
} vm_prot_t;

typedef enum {
    VM_MEM_NORMAL = 0,
    VM_MEM_DEVICE = 1,
} vm_memtype_t;

typedef enum {
    VM_OWNER_NONE    = 0, // Physical pages NOT owned by this addrspace.
                           // Used for MMIO, device memory, external allocations.
                           // On destroy: unmap only, do NOT free pages.
    VM_OWNER_ANON    = 1, // Physical pages allocated by PMM for this addrspace.
                           // Used for anonymous memory (heap, stack, user allocations).
                           // On destroy: must walk page tables, translate VA→PA, free pages to PMM.
    VM_OWNER_SHARED  = 2, // Physical pages owned by a different addrspace or subsystem.
                           // Used for shared kernel mappings, copy-on-write, etc.
                           // On destroy: unmap only, do NOT free pages.
} vm_owner_t;

typedef enum {
    VM_FLAG_NONE   = 0,
    VM_FLAG_PINNED = 1u << 0, // must stay mapped
    VM_FLAG_GLOBAL = 1u << 1, // global TLB entry where supported
    VM_FLAG_GUARD  = 1u << 2, // guard page/region
    VM_FLAG_TEMPORARY = 1u << 3, // temporary mapping (e.g. identity map during boot)
} vm_flags_t;

typedef struct vm_region {
    uintptr_t    vaddr_start;
    size_t       size;
    vm_prot_t    prot;
    vm_memtype_t memtype;
    vm_owner_t   owner;        // ownership: who allocated/owns the backing pages
    uintptr_t    paddr_start;
    vm_flags_t   flags;
} vm_region_t;

typedef enum {
    ADDRSPACE_KERNEL = 0,
    ADDRSPACE_USER   = 1,
} addrspace_type_t;

typedef struct addrspace {
    uintptr_t        ttbr0_pa;   // physical address of level-1 table
    vm_region_t     *regions;
    size_t          region_count;
    //uint32_t         lock;      // placeholder until concurrency is added
    addrspace_type_t type;
} addrspace_t;


#define IOREMAP_BASE    0xF0000000
#define IOREMAP_END     0xFFFFFFFF  
#define IOREMAP_SIZE    (IOREMAP_END - IOREMAP_BASE + 1)  // 256MB
#define IOREMAP_SLOTS   (IOREMAP_SIZE / SECTION_SIZE)     // 256


// Mapping table: track active mappings for iounmap lookup
#define IOREMAP_MAX_ENTRIES 64


/**
 * @brief Create a new address space.
 * @param type ADDRSPACE_KERNEL or ADDRSPACE_USER.
 * @return Pointer to the newly created address space, or NULL on failure.
 */
addrspace_t* addrspace_create(addrspace_type_t type);

/**
 * @brief Destroy an address space.
 * @param as Address space to destroy.
 * Responsibilities: unmap regions, free page tables, release physical memory.
 */
void addrspace_destroy(addrspace_t* as);

/**
 * @brief Add a region to an address space.
 * @param as Address space.
 * @param region Region to add (vaddr_start, size, prot, memtype, paddr_start, flags).
 * @return true on success, false if overlap or alignment error.
 * Does not touch page tables; only validates and appends to as->regions.
 */
bool vmm_add_region(addrspace_t* as, const vm_region_t* region);

/**
 * @brief Remove a region from an address space.
 * @param as Address space.
 * @param vaddr Virtual address of the region to remove.
 * @param size Size of the region.
 * @return true on success, false if not found.
 */
bool vmm_remove_region(addrspace_t* as, uintptr_t vaddr, size_t size);

/**
 * @brief Build actual page tables from region descriptions.
 * @param as Address space to realize.
 * Iterates over all vm_region_t in as->regions and calls arch_mmu_map().
 * @return true on success, false on page table allocation failure.
 */
bool vmm_build_page_tables(addrspace_t* as);

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
void vmm_bootstrap(void);

/**
 * @brief Activate/switch to an address space.
 * @param as Address space to activate.
 * Calls arch layer to load TTBR0 and flush TLB.
 * Used for context switching and userspace entry.
 */
void vmm_activate(addrspace_t* as);

/**
 * @brief High-level mapping API: add a single mapping to an address space.
 * @param as Address space.
 * @param va Virtual address (should be page-aligned).
 * @param pa Physical address (should be page-aligned).
 * @param size Size in bytes (should be page-aligned or section-aligned).
 * @param prot Protection flags (VM_PROT_READ | VM_PROT_WRITE | ...).
 * @param memtype VM_MEM_NORMAL or VM_MEM_DEVICE.
 * @param owner VM_OWNER_* (determines if pages are freed on destroy).
 * @param flags VM_FLAG_* bits (pinned, global, guard, etc.).
 * @return true on success, false on error.
 */
bool vmm_map_range(addrspace_t* as, uintptr_t va, uintptr_t pa, size_t size,
                   vm_prot_t prot, vm_memtype_t memtype, vm_owner_t owner, vm_flags_t flags);

/**
 * @brief Remove mappings from an address space.
 * @param as Address space.
 * @param va Virtual address.
 * @param size Size in bytes.
 * @return true on success, false if not found.
 *
 * Responsibilities (what this function does):
 *   - Clear page table entries for the VA range
 *   - Invalidate TLB entries (via arch_mmu_unmap)
 *   - Optionally free empty L2 tables (architecture-dependent)
 *
 * What this function does NOT do:
 *   - Does NOT free physical pages back to PMM
 *   - Does NOT unmark pages in PMM
 *
 * Contract: The caller is responsible for freeing physical pages.
 * If the region owns its pages (VM_OWNER_ANON), the caller must walk
 * page tables BEFORE unmapping to discover which PAs to free.
 *
 * TLB Handling: vmm_unmap_range calls arch_mmu_unmap, which handles
 * TLB invalidation. If the addrspace is active (TTBR0), the TLB must
 * be invalidated for this to take effect.
 */
bool vmm_unmap_range(addrspace_t* as, uintptr_t va, size_t size);

/**
 * @brief Change permissions on a range.
 * @param as Address space.
 * @param va Virtual address.
 * @param size Size in bytes.
 * @param new_prot New protection flags.
 * @return true on success, false if not found.
 */
bool vmm_protect_range(addrspace_t* as, uintptr_t va, size_t size, vm_prot_t new_prot);

/**
 * @brief Map device memory into kernel VA space.
 * @param pa Physical address of the device.
 * @param size Size in bytes.
 * @return Virtual address of the mapping, or 0 on failure.
 */
uintptr_t kmap_mmio(uintptr_t pa, size_t size);

/**
 * @brief Map a page into user address space.
 * @param as User address space.
 * @param pa Physical address of the page.
 * @param va Virtual address in user space.
 * @param prot Protection flags.
 * @return true on success, false on error.
 */
bool kmap_user_page(addrspace_t* as, uintptr_t pa, uintptr_t va, vm_prot_t prot);

/**
 * @brief Remove the identity mapping from the kernel address space.
 * After calling this, the kernel runs purely in higher-half memory.
 */
void vmm_remove_identity_mapping(void);

/**
 * @brief Map a physical address range into kernel virtual address space for I/O.
 * @param phys Physical address to map.
 * @param size Size of the mapping.
 * @return Virtual address of the mapped region, or NULL on failure.
 */
void* ioremap(uintptr_t phys, size_t size);

/**
 * @brief Unmap a previously mapped I/O region.
 * @param va Virtual address returned by ioremap.
 */
void iounmap(void* va);

#endif // KERNEL_VM_VMM_H