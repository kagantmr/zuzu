/**
 * @file vmm.h
 * @brief Virtual memory manager: address spaces, regions, mappings, and ioremap.
 *
 * An address space owns a set of page tables and a sorted list of regions that describe what each
 * range of virtual addresses is for. Pages in user regions are mapped lazily by the page fault
 * path; device and kernel mappings are installed eagerly.
 */

#ifndef KERNEL_MM_VMM_VMM_H
#define KERNEL_MM_VMM_VMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <vector.h>
#include BOARD_LAYOUT_H
#include <arch/asid.h>

/** @brief Convert a physical address to its kernel higher-half virtual address. */
#define PA_TO_VA(pa) ((VirtAddr)(pa) + KERNEL_VA_OFFSET)
/** @brief Convert a higher-half kernel virtual address to its physical address. */
#define VA_TO_PA(va) ((PhysAddr)(va) - KERNEL_VA_OFFSET)

/** @brief Forward declaration of a space object. */
typedef struct SpaceObjectStruct SpaceObject;
/** @brief Forward declaration of a handle table entry. */
typedef struct HandleTableEntryStruct HandleTableEntry;

/** @brief Most ioremap mappings that can be live at once. */
#define IOREMAP_MAX_ENTRIES 16

/** @brief Protection bit that makes a mapping accessible from user mode. */
#define VM_PROT_USER (1U << 3)

/** @brief Memory attributes of a mapping. */
typedef enum {
    VM_MEM_NORMAL = 0,    /**< Normal cacheable memory. */
    VM_MEM_DEVICE = 1,    /**< Device memory, never executable. */
    VM_MEM_NORMAL_NC = 2  /**< Normal memory with caching disabled, never executable. */
} VirtMemType;

/** @brief Where the pages of a region come from. */
typedef enum {
    VM_BACKING_NONE = 0,   /**< Mapped eagerly with no demand paging, so a fault is not served. */
    VM_BACKING_ANON = 1,   /**< Zero-filled frames allocated on first touch. */
    VM_BACKING_SHARED = 2, /**< Pages of a shared memory object, allocated on first touch if absent. */
} VirtMemBacking;

/** @brief Per-region flags. */
typedef enum {
    VM_FLAG_NONE = 0,         /**< No flags. */
    VM_FLAG_PINNED = (1U << 0), /**< The region cannot be unmapped or have its protection changed. */
    VM_FLAG_GUARD = (1U << 2),  /**< Guard region; any access to it is rejected. */
} VirtMemFlags;

/**
 * @brief A contiguous range of virtual addresses with uniform attributes.
 *
 * A region holds a reference to its backing object, taken when it is added to an address space and
 * dropped when it is removed.
 */
typedef struct VirtMemRegionStruct {
    VirtAddr vaddr_start;  /**< First virtual address of the region. */
    size_t size;           /**< Length in bytes. */
    MemProt prot;          /**< Current protection. */
    MemProt max_prot;      /**< Ceiling that Protect may not exceed; defaults to the low rwx bits of prot when 0. */
    VirtMemType memtype;   /**< Memory attributes. */
    VirtMemBacking owner;  /**< Source of the region's pages. */
    VirtMemFlags flags;    /**< Region flags. */
    void *backing;         /**< Backing MemObject, or NULL. */
    size_t backing_page_offset; /**< Index of the first page of the backing object that the region maps. */
} VirtMemRegion;

/** @brief Kind of address space. */
typedef enum {
    ADDRESS_SPACE_KERNEL = 0, /**< The kernel address space. */
    ADDRESS_SPACE_USER = 1,   /**< A user address space with its own ASID. */
} AddressSpaceType;

/** @brief Growable vector of regions, kept sorted by start address. */
DEFINE_VEC(vm_region, VirtMemRegion)

/** @brief A set of page tables plus the regions that describe them. */
typedef struct AddressSpaceStruct {
    PhysAddr pt_root_physaddr; /**< Physical address of the top-level page table. */
    vm_region_vec_t regions;   /**< Regions sorted by start address, never overlapping. */
    AddressSpaceType type;     /**< Kernel or user. */
    AsidToken asid_token;      /**< ASID held by the space; zero for the kernel space. */
} AddressSpace;

/** @brief Size in bytes of the ioremap window. */
#define IOREMAP_SIZE (IOREMAP_END - IOREMAP_BASE + 1)
/** @brief Number of section-sized slots in the ioremap window. */
#define IOREMAP_SLOTS (IOREMAP_SIZE / SECTION_SIZE)

/** @brief Number of ioremap slots usable before the kernel stack region begins. */
#define IOREMAP_MAX_SLOT ((KSTACK_REGION_BASE - IOREMAP_BASE) / SECTION_SIZE)

/**
 * @brief Get the kernel address space.
 *
 * @return The kernel address space, or NULL before VmmBootstrap() has run.
 */
AddressSpace *VmmGetKernelAddressSpace(void);

/**
 * @brief Create an address space with empty page tables.
 *
 * User spaces are also given an ASID.
 *
 * @param[in] type Kind of space to create.
 * @return The new space, or NULL if tables, an ASID, or memory could not be allocated.
 */
AddressSpace *AddressSpaceCreate(AddressSpaceType type);

/**
 * @brief Tear down an address space.
 *
 * Unmaps every region, drops the references to their backing objects, and frees the ASID and page
 * tables. Panics if the space is currently active.
 *
 * @param[in] as The space to destroy; NULL is ignored.
 */
void AddressSpaceDestroy(AddressSpace *as);

/**
 * @brief Insert a region into an address space, keeping the list sorted.
 *
 * The region is copied and a reference is taken on its backing object. If max_prot is 0 it is set
 * to the rwx bits of prot. Nothing is mapped; pages arrive through the fault path or a separate
 * VmmMapRange() call.
 *
 * @param[in,out] as The space to add the region to.
 * @param[in] region The region to copy in.
 * @return true on success.
 * @retval false The arguments are invalid, the region has zero size, it overlaps an existing
 *               region, or the region list could not grow.
 */
bool VmmAddRegion(AddressSpace *restrict as, const VirtMemRegion *restrict region);

/**
 * @brief Unmap and remove a region, dropping its reference to the backing object.
 *
 * @param[in,out] as The space holding the region.
 * @param[in] vaddr Start address of the region.
 * @param[in] size Size of the region, which must match exactly.
 * @return true if the region was removed.
 * @retval false No region starts at vaddr with that size.
 */
bool VmmRemoveRegion(AddressSpace *as, uintptr_t vaddr, size_t size);

/**
 * @brief Find a gap between existing regions that fits a size.
 *
 * @param[in] as The space to search.
 * @param[in] lo Lowest address to consider.
 * @param[in] hi End of the search window.
 * @param[in] size Length needed in bytes.
 * @return The start of the first gap that fits, or 0 if there is none.
 */
VirtAddr VmmFindFreeVa(const AddressSpace *as, VirtAddr lo, VirtAddr hi, size_t size);

/**
 * @brief Build the kernel address space and switch to it.
 *
 * Copies the early boot tables, registers the higher-half RAM mapping and the boot identity
 * mapping as regions, and marks the MMU as enabled. Panics on allocation failure. Does nothing if
 * the kernel space already exists.
 */
void VmmBootstrap(void);

/**
 * @brief Make an address space the active one.
 *
 * Enables the MMU on the first call and switches tables afterwards. Does nothing if the space is
 * already active.
 *
 * @param[in] as The space to activate; NULL is ignored.
 */
void VmmActivateAddressSpace(AddressSpace *as);

/**
 * @brief Map a physically contiguous range into an address space.
 *
 * @param[in,out] as The space to map into.
 * @param[in] va Page-aligned virtual start address.
 * @param[in] pa Page-aligned physical start address.
 * @param[in] size Length in bytes, non-zero.
 * @param[in] prot Protection for the mapping.
 * @param[in] memtype Memory attributes.
 * @return true on success.
 * @retval false A parameter is invalid, a user mapping is writable and executable, a user mapping
 *               reaches USER_VA_TOP, or the architecture layer failed.
 */
bool VmmMapRange(AddressSpace *as, VirtAddr va, PhysAddr pa, size_t size, MemProt prot,
                 VirtMemType memtype);

/**
 * @brief Unmap a range from an address space.
 *
 * @param[in,out] as The space to unmap from.
 * @param[in] va Page-aligned virtual start address.
 * @param[in] size Page-aligned length in bytes, non-zero.
 * @param[in] flush Whether to flush the TLB for the range.
 * @return true on success.
 * @retval false A parameter is invalid or the architecture layer failed.
 */
bool VmmUnmapRange(AddressSpace *as, VirtAddr va, size_t size, bool flush);

/**
 * @brief Change the protection of part of a region.
 *
 * The range must lie inside one region that is not pinned. Device and uncached regions cannot be
 * made executable. On success the region's recorded protection is updated to new_prot. This does
 * not check max_prot; VmmProtectUserRange() does.
 *
 * @param[in,out] as The space holding the range.
 * @param[in] va Start of the range.
 * @param[in] size Length in bytes.
 * @param[in] new_prot The new protection.
 * @return true on success.
 * @retval false No region contains the range, the region is pinned, the request is illegal for
 *               its memory type, or the architecture layer failed.
 */
bool VmmProtectPage(AddressSpace *as, VirtAddr va, size_t size, MemProt new_prot);

/**
 * @brief Map one normal-memory page into a user address space.
 *
 * @param[in,out] as The user space to map into.
 * @param[in] pa Page-aligned physical address.
 * @param[in] va Page-aligned virtual address.
 * @param[in] prot Protection; the user bit is added.
 * @return true on success.
 * @retval false as is not a user space, an address is unaligned, or the mapping failed.
 */
bool VmmMapUserPage(AddressSpace *as, PhysAddr pa, VirtAddr va, MemProt prot);

/**
 * @brief Create an anonymous, demand-paged user region.
 *
 * The region may later be given any combination of rwx through Protect. At most 32 MiB can be
 * mapped in one call.
 *
 * @param[in,out] space The space to map into.
 * @param[in] hint Requested address, or 0 to let the kernel choose one in the mmap window.
 * @param[in] size Page-aligned length in bytes.
 * @param[in] prot Initial protection.
 * @param[out] out Receives the start address of the region.
 * @return ZUZU_OK on success.
 * @retval ERR_BADARG The size is zero or unaligned, or the hint is unaligned or out of range.
 * @retval ERR_OVERFLOW The size exceeds the per-call limit.
 * @retval ERR_NOMEM No free address range, or the region could not be added.
 */
Err VmmMapAnon(SpaceObject *space, VirtAddr hint, size_t size, MemProt prot, VirtAddr *out);

/**
 * @brief Map the memory object behind a handle into a user space.
 *
 * Shared objects become demand-paged regions in the mmap window and device objects are mapped
 * eagerly in the device window. The region's max_prot comes from the handle's permissions, and the
 * handle records the mapped address.
 *
 * @param[in,out] space The space to map into.
 * @param[in,out] entry Handle table entry that holds the memory object.
 * @param[in] prot Requested protection, which must be within the handle's permissions.
 * @param[in] hint Requested address, or 0 to let the kernel choose one.
 * @param[out] out Receives the start address of the mapping.
 * @return ZUZU_OK on success.
 * @retval ERR_NOPERM The protection exceeds the handle's permissions, or an uncached object was
 *                    requested executable.
 * @retval ERR_BUSY The handle is already mapped.
 * @retval ERR_BADHANDLE The entry holds no memory object.
 * @retval ERR_BADARG The hint is unaligned or outside the window for the object kind.
 * @retval ERR_NOMEM No free address range, or the mapping could not be installed.
 * @retval ERR_BADTYPE The object kind cannot be mapped.
 */
Err VmmMapMemObj(SpaceObject *space, HandleTableEntry *entry, MemProt prot, VirtAddr hint,
                 VirtAddr *out);

/**
 * @brief Unmap the user region that starts at an address.
 *
 * Anonymous regions have their resident frames freed, and the mapped address is cleared on the
 * handle that mapped a memory object.
 *
 * @param[in,out] space The space holding the region.
 * @param[in] va Start address of the region.
 * @return ZUZU_OK on success.
 * @retval ERR_NOENT No region contains va.
 * @retval ERR_BADARG va is not the start of the region.
 * @retval ERR_NOPERM The region is pinned.
 */
Err VmmUnmapUserRegion(SpaceObject *space, VirtAddr va);

/**
 * @brief Install a region into a space that has no tasks yet, on behalf of its parent.
 *
 * The source is either anonymous memory or a range of a shared memory object reachable through a
 * handle in the parent. Pages of a shared object that already exist are mapped immediately. The
 * region's max_prot equals the requested protection.
 *
 * @param[in,out] kitten The new space to inject into.
 * @param[in] parent The space whose handle table is used to look up the memory object.
 * @param[in] args Destination, length, offset, protection, and source handle.
 * @return ZUZU_OK on success.
 * @retval ERR_BADARG Unaligned or out-of-range arguments, or an illegal protection.
 * @retval ERR_BUSY The target space already has tasks.
 * @retval ERR_BADHANDLE The source handle is invalid.
 * @retval ERR_BADTYPE The handle is not a shared memory object.
 * @retval ERR_NOPERM The handle lacks map permission for the requested protection.
 * @retval ERR_NOMEM The region could not be added or mapped.
 */
Err InjectObjIntoSpace(SpaceObject *kitten, SpaceObject *parent, const InjectObjArgs *args);

/**
 * @brief Change the protection of a user range, bounded by the region's max_prot.
 *
 * @param[in,out] space The space holding the range.
 * @param[in] va Page-aligned start of the range.
 * @param[in] size Page-aligned length in bytes.
 * @param[in] new_prot New protection; writable and executable together is refused.
 * @return ZUZU_OK on success.
 * @retval ERR_BADARG Unaligned or out-of-range arguments, an illegal protection, or no region.
 * @retval ERR_NOPERM new_prot exceeds the region's max_prot.
 */
Err VmmProtectUserRange(SpaceObject *space, VirtAddr va, size_t size, MemProt new_prot);

/**
 * @brief Drop the boot-time identity mapping of RAM.
 *
 * Relocates stacks to the higher half first if the stack pointer is still in the identity range.
 * Afterwards the kernel runs purely in the higher half.
 */
void VmmRemoveIdentityMapping(void);

/**
 * @brief Map a physical device range into the kernel ioremap window as device memory.
 *
 * Mapping is done in whole sections, so the returned pointer carries the offset of phys within its
 * section.
 *
 * @param[in] phys Physical start address.
 * @param[in] size Length in bytes.
 * @return A virtual address for phys, or NULL if size is zero or no window space or table entry is
 *         free.
 */
void *IoRemap(PhysAddr phys, size_t size);

/**
 * @brief Release a mapping made by IoRemap().
 *
 * @param[in] va The pointer IoRemap() returned; NULL and unknown addresses are ignored.
 */
void IoUnmap(void *va);

/**
 * @brief Check that a range is accessible, faulting in missing pages.
 *
 * Every page not yet mapped must belong to a non-guard region with the needed access, and is then
 * populated through VmmPageFaultHandle().
 *
 * @param[in,out] as The space to check.
 * @param[in] va Start of the range.
 * @param[in] len Length in bytes; 0 always succeeds.
 * @param[in] write Whether write access is needed.
 * @return true if the whole range is accessible.
 * @retval false The range wraps or leaves user space, a page has no region or insufficient
 *               permission, or populating a page failed.
 */
bool VmmCheckUserFault(AddressSpace *as, VirtAddr va, size_t len, bool write);

/**
 * @brief Populate one page of a region.
 *
 * Anonymous regions get a zeroed frame. Shared regions use, or allocate and zero, the page at the
 * matching index of the backing object. Device and unbacked regions are not served.
 *
 * @param[in,out] as The space that faulted.
 * @param[in,out] r The region containing page_va.
 * @param[in] page_va Page-aligned faulting address.
 * @return true if the page is mapped on return, including when it already was.
 * @retval false The region cannot be demand-paged, the index is outside the backing object, or
 *               allocation or mapping failed.
 */
bool VmmPageFaultHandle(AddressSpace *restrict as, VirtMemRegion *restrict r,
                        VirtAddr page_va);

/**
 * @brief Get the protection of the region that contains @p va.
 *
 * @param[in]  as  The address space.
 * @param[in]  va  An address inside the region.
 * @param[out] out The region's protection.
 * @return true if @p va is inside a region.
 */
bool VmmRegionProt(AddressSpace *as, VirtAddr va, MemProt *out);

/**
 * @brief Make the kernel image mappings privileged read-only at the page-table level.
 *
 * Rewrites the access permission bits of the section and small-page entries that cover the kernel
 * image, then flushes the TLB.
 */
void VmmLockdownKernelMapping(void);

#endif // KERNEL_MM_VMM_VMM_H
