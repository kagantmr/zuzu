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

#define VM_PROT_USER (1U << 3)

typedef enum {
    VM_MEM_NORMAL = 0,
    VM_MEM_DEVICE = 1,
    VM_MEM_NORMAL_NC = 2
} VirtMemType;

typedef enum {
    VM_BACKING_NONE = 0,
    VM_BACKING_ANON = 1,
    VM_BACKING_SHARED = 2,
} VirtMemBacking;

typedef enum {
    VM_FLAG_NONE = 0,
    VM_FLAG_PINNED = (1U << 0),
    VM_FLAG_GUARD = (1U << 2),
} VirtMemFlags;

typedef struct VirtMemRegionStruct {
    VirtAddr vaddr_start;
    size_t size;
    MemProt prot;
    VirtMemType memtype;
    VirtMemBacking owner;
    VirtMemFlags flags;
    void *backing;
    size_t backing_page_offset;
} VirtMemRegion;

typedef enum {
    ADDRESS_SPACE_KERNEL = 0,
    ADDRESS_SPACE_USER = 1,
} AddressSpaceType;

DEFINE_VEC(vm_region, VirtMemRegion)

typedef struct AddressSpaceStruct {
    PhysAddr pt_root_physaddr;
    vm_region_vec_t regions;
    AddressSpaceType type;
    AsidToken asid_token;
} AddressSpace;

#define IOREMAP_SIZE (IOREMAP_END - IOREMAP_BASE + 1)
#define IOREMAP_SLOTS (IOREMAP_SIZE / SECTION_SIZE)

#define IOREMAP_MAX_SLOT ((KSTACK_REGION_BASE - IOREMAP_BASE) / SECTION_SIZE)

AddressSpace *VmmGetKernelAddressSpace(void);

AddressSpace *AddressSpaceCreate(AddressSpaceType type);

void AddressSpaceDestroy(AddressSpace *as);

bool VmmAddRegion(AddressSpace *restrict as, const VirtMemRegion *restrict region);

bool VmmRemoveRegion(AddressSpace *as, VirtAddr vaddr, size_t size);

VirtAddr VmmFindFreeVa(const AddressSpace *as, VirtAddr lo, VirtAddr hi, size_t size);

void VmmBootstrap(void);

void VmmActivateAddressSpace(AddressSpace *as);

bool VmmMapRange(AddressSpace *as, VirtAddr va, PhysAddr pa, size_t size, MemProt prot,
                 VirtMemType memtype);

bool VmmUnmapRange(AddressSpace *as, VirtAddr va, size_t size, bool flush);

bool VmmProtectPage(AddressSpace *as, VirtAddr va, size_t size, MemProt new_prot);

bool VmmMapUserPage(AddressSpace *as, PhysAddr pa, VirtAddr va, MemProt prot);

Err VmmMapAnon(SpaceObject *space, VirtAddr hint, size_t size, MemProt prot, VirtAddr *out);

Err VmmMapMemObj(SpaceObject *space, HandleTableEntry *entry, MemProt prot, VirtAddr hint,
                 VirtAddr *out);

Err VmmUnmapUserRegion(SpaceObject *space, VirtAddr va);

Err InjectObjIntoSpace(SpaceObject *kitten, SpaceObject *parent, const InjectObjArgs *args);

Err VmmProtectUserRange(SpaceObject *space, VirtAddr va, size_t size, MemProt new_prot);

void VmmRemoveIdentityMapping(void);

void *IoRemap(PhysAddr phys, size_t size);

void IoUnmap(void *va);

bool VmmCheckUserFault(AddressSpace *as, VirtAddr va, size_t len, bool write);

bool VmmPageFaultHandle(AddressSpace *restrict as, VirtMemRegion *restrict region,
                        uintptr_t page_va);

void VmmLockdownKernelMapping(void);

#endif // KERNEL_MM_VMM_VMM_H
