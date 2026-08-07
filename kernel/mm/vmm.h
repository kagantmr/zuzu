#ifndef KERNEL_VM_VMM_H
#define KERNEL_VM_VMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <zuzu/memprot.h>
#include <vector.h>
#include <zuzu/types.h>
#include BOARD_LAYOUT_H
#include <arch/asid.h>

#define PA_TO_VA(pa) ((VirtAddr)(pa) + KERNEL_VA_OFFSET)
#define VA_TO_PA(va) ((PhysAddr)(va) - KERNEL_VA_OFFSET)

#define IOREMAP_MAX_ENTRIES 16 // was 64

#define VM_PROT_USER 1u << 3 // user-accessible (otherwise kernel-only)

typedef enum
{
    VM_MEM_NORMAL = 0,
    VM_MEM_DEVICE = 1,
} vm_memtype_t;

typedef enum
{
    VM_OWNER_NONE = 0,   // Physical pages NOT owned by this addrspace.
                         // Used for MMIO, device memory, external allocations.
                         // On destroy: unmap only, do NOT free pages.
    VM_OWNER_ANON = 1,   // Physical pages allocated by PMM for this addrspace.
                         // Used for anonymous memory (heap, stack, user allocations).
                         // On destroy: must walk page tables, translate VA→PA, free pages to PMM.
    VM_OWNER_SHARED = 2, // Physical pages owned by a different addrspace or subsystem.
                         // Used for shared kernel mappings, copy-on-write, etc.
                         // On destroy: unmap only, do NOT free pages.
} vm_owner_t;

typedef enum
{
    VM_FLAG_NONE = 0,
    VM_FLAG_PINNED = 1u << 0,    // must stay mapped
    VM_FLAG_GLOBAL = 1u << 1,    // global TLB entry where supported
    VM_FLAG_GUARD = 1u << 2,     // guard page/region
    VM_FLAG_TEMPORARY = 1u << 3, // temporary mapping (e.g. identity map during boot)
} vm_flags_t;

typedef struct vm_region
{
    uintptr_t vaddr_start;
    size_t size;
    MemProt prot;
    vm_memtype_t memtype;
    vm_owner_t owner; // ownership: who allocated/owns the backing pages
    uintptr_t paddr_start;
    vm_flags_t flags;
    void *backing; // optional pointer to backing object (e.g. ShmCap) for shared memory, file mappings, etc.
} vm_region_t;

typedef enum
{
    ADDRSPACE_KERNEL = 0,
    ADDRSPACE_USER = 1,
} addrspace_type_t;

DEFINE_VEC(vm_region, vm_region_t)

typedef struct addrspace
{
    PhysAddr ttbr_pa; // physical address of level-1 table
    vm_region_vec_t regions;
    // uint32_t         lock;      // placeholder until concurrency is added
    addrspace_type_t type;
    asid_token_t asid_token;
} addrspace_t;

typedef struct
{
    PhysAddr *page_addrs; // array of individual PAs, one per page
    size_t page_count;     // amount of used pages
    size_t ref_count;    // live HANDLE references (shm_create + each grant). NOT mappings. Object frees when this hits zero.
} ShmCap;

/* Drop one handle reference to a shmem object. shm_create and each grant add
 * one; SysDestroy and process teardown drop one. */
void shmem_drop_ref(ShmCap *shm);

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
 * above), but a _Static_assert here would evaluate too early. See vmm.c for
 * the assert once both headers are actually in scope. */
#define IOREMAP_MAX_SLOT ((KSTACK_REGION_BASE - IOREMAP_BASE) / SECTION_SIZE)

addrspace_t *vmm_get_kernel_as(void);

/**
 * @brief Create a new address space.
 * @param type ADDRSPACE_KERNEL or ADDRSPACE_USER.
 * @return Pointer to the newly created address space, or NULL on failure.
 */
addrspace_t *as_create(addrspace_type_t type);

/**
 * @brief Destroy an address space.
 * @param as Address space to destroy.
 * Responsibilities: unmap regions, free page tables, release physical memory.
 */
void as_destroy(addrspace_t *as);

/**
 * @brief Add a region to an address space.
 * @param as Address space.
 * @param region Region to add (vaddr_start, size, prot, memtype, paddr_start, flags).
 * @return true on success, false if overlap or alignment error.
 * Does not touch page tables; only validates and appends to as->regions.
 */
/* region is always a stack/global compound literal, never an alias of
 * anything reachable through as (in particular never a pointer into
 * as->regions.data itself). */
bool vmm_add_region(addrspace_t *restrict as, const vm_region_t *restrict region);

/**
 * @brief Remove a region from an address space.
 * @param as Address space.
 * @param vaddr Virtual address of the region to remove.
 * @param size Size of the region.
 * @return true on success, false if not found.
 */
bool vmm_remove_region(addrspace_t *as, VirtAddr vaddr, size_t size);

/**
 * @brief Build actual page tables from region descriptions.
 * @param as Address space to realize.
 * Iterates over all vm_region_t in as->regions and calls arch_mmu_map().
 * @return true on success, false on page table allocation failure.
 */
bool vmm_build_page_tables(addrspace_t *as);

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
void vmm_activate(addrspace_t *as);

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
bool vmm_map_range(addrspace_t *as, VirtAddr va, PhysAddr pa, size_t size,
                   MemProt prot, vm_memtype_t memtype, vm_owner_t owner, vm_flags_t flags);

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
bool vmm_unmap_range(addrspace_t *as, VirtAddr va, size_t size);

/**
 * @brief Change permissions on a range.
 * @param as Address space.
 * @param va Virtual address.
 * @param size Size in bytes.
 * @param new_prot New protection flags.
 * @return true on success, false if not found.
 */
bool vmm_protect_range(addrspace_t *as, VirtAddr va, size_t size, MemProt new_prot);

/**
 * @brief Map a page into user address space.
 * @param as User address space.
 * @param pa Physical address of the page.
 * @param va Virtual address in user space.
 * @param prot Protection flags.
 * @return true on success, false on error.
 */
bool vmm_map_user_page(addrspace_t *as, PhysAddr pa, VirtAddr va, MemProt prot);

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
void *ioremap(PhysAddr phys, size_t size);

/**
 * @brief Unmap a previously mapped I/O region.
 * @param va Virtual address returned by ioremap.
 */
void iounmap(void *va);

bool fault_in_pages(addrspace_t *as, VirtAddr va, size_t len, bool write);
/* region always points into as->regions.data, but the struct addrspace_t
 * bytes themselves (ttbr_pa, the regions vector header) and the
 * vm_region_t bytes region points at never overlap -- on the lazy-mapping
 * hot path (try_demand_page -> here), this is what makes it safe. */
bool vmm_fault_page(addrspace_t *restrict as, vm_region_t *restrict region, uintptr_t page_va);

void vmm_lockdown_kernel_sections(void);

#endif // KERNEL_VM_VMM_H