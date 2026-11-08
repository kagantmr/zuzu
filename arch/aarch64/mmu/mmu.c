/**
 * @file mmu.c
 * @brief AArch64 translation tables: creating, mapping, protecting and tearing down, and TLB
 * maintenance.
 */

#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
#include <arch/asid.h>
#include <arch/mmu.h>
#include <arch_impl/aarch64_mmu.h>
#include <arch_impl/barrier.h>
#include <arch_impl/regs.h>
#include <arch_impl/sysreg.h>
#include <string.h>
#include <zuzu/user_layout.h>

#include BOARD_BOOT_H

/**
 * @brief Callback run by WalkRange() on each leaf entry.
 *
 * @param[in,out] slot  The leaf entry: a page at level 3, a block at level 1 or 2.
 * @param[in]     level Level of the entry.
 * @param[in]     va    Address being walked inside the entry; not rounded down to its start.
 * @param[in,out] ctx   Caller's context.
 */
typedef void (*WalkFn)(AArch64Pte *slot, int level, VirtAddr va, void *ctx);

/**
 * @brief Returns the root table of an address space through the linear map.
 *
 * @param[in] as Address space.
 * @return The level 0 table.
 */
static inline AArch64Pte *RootOf(const AddressSpace *as)
{
    return (AArch64Pte *)PA_TO_VA(as->pt_root_physaddr);
}

/**
 * @brief Returns the table an entry points to through the linear map.
 *
 * @param[in] e Table entry.
 * @return The next-level table.
 */
static inline AArch64Pte *TableOf(AArch64Pte e)
{
    return (AArch64Pte *)PA_TO_VA(e & PTE_ADDR_MASK);
}

/**
 * @brief Allocates one zeroed frame for a translation table.
 *
 * @return Its physical address, or PA_NULL when out of memory.
 */
static PhysAddr AllocTable(void)
{
    PhysAddr pa = PmmAllocFrame();
    if (pa != PA_NULL)
        memset((void *)PA_TO_VA(pa), 0, PAGE_SIZE);
    return pa;
}

/**
 * @brief Builds the attribute bits of a page or block entry, without the address and type bits.
 *
 * Device and non-cacheable memory is never executable. User memory is never executable at EL1.
 *
 * @param[in] prot    PROT_* flags, plus VM_PROT_USER for EL0 access.
 * @param[in] memtype Memory type.
 * @return The attribute bits.
 */
static AArch64Pte ProtToDesc(MemProt prot, VirtMemType memtype)
{
    AArch64Pte pte = PTE_AF;

    switch (memtype) {
    case VM_MEM_NORMAL:
        pte |= PTE_ATTRINDX(PTE_ATTRINDX_NORMAL) | PTE_SH_INNER;
        break;
    case VM_MEM_DEVICE:
        pte |= PTE_ATTRINDX(PTE_ATTRINDX_DEVICE) | PTE_PXN | PTE_UXN;
        break;
    case VM_MEM_NORMAL_NC:
        pte |= PTE_ATTRINDX(PTE_ATTRINDX_NORMAL_NC) | PTE_PXN | PTE_UXN;
        break;
    }

    if (!(prot & PROT_WRITE))
        pte |= PTE_AP_RO;

    if (prot & VM_PROT_USER)
        pte |= PTE_AP_EL0 | PTE_NG | PTE_PXN;

    if (!(prot & PROT_EXEC))
        pte |= PTE_UXN | PTE_PXN;
    else if (!(prot & VM_PROT_USER))
        pte |= PTE_UXN;

    return pte;
}

/**
 * @brief Follows a table entry, creating the table it should point to if the entry is empty.
 *
 * @param[in,out] entry Entry at level 0, 1 or 2.
 * @return The next-level table, or NULL if the entry is a block or no frame is free.
 */
static AArch64Pte *NextTable(AArch64Pte *entry)
{
    if (!(*entry & PTE_VALID)) {
        PhysAddr pa = AllocTable();
        if (pa == PA_NULL)
            return NULL;
        *entry = pa | PTE_VALID | PTE_TABLE_OR_PAGE;
    } else if (!(*entry & PTE_TABLE_OR_PAGE)) {
        return NULL;
    }

    return TableOf(*entry);
}

/**
 * @brief Finds the entry for @p va at @p level, creating the tables above it on the way.
 *
 * @param[in,out] root  Level 0 table.
 * @param[in]     va    Virtual address.
 * @param[in]     level Level of the entry wanted, 1 to 3.
 * @return The entry, or NULL if a block is in the way or no frame is free.
 */
static AArch64Pte *WalkTo(AArch64Pte *root, VirtAddr va, int level)
{
    AArch64Pte *table = root;
    for (int l = 0; l < level; l++) {
        table = NextTable(&table[PteIndex(va, l)]);
        if (!table)
            return NULL;
    }

    return &table[PteIndex(va, level)];
}

/**
 * @brief Finds the leaf entry that maps @p va, without creating anything.
 *
 * @param[in]  root      Level 0 table.
 * @param[in]  va        Virtual address.
 * @param[out] level_out Level of the entry found: 3 for a page, 1 or 2 for a block.
 * @return The entry, or NULL if @p va is not mapped.
 */
static AArch64Pte *Lookup(AArch64Pte *root, VirtAddr va, int *level_out)
{
    AArch64Pte *table = root;
    for (int level = 0; level <= PTE_LEVEL_LAST; level++) {
        AArch64Pte *slot = &table[PteIndex(va, level)];
        AArch64Pte e = *slot;

        if (!(e & PTE_VALID))
            return NULL;

        if (!PteIsTable(e, level)) {
            if (level == 0)
                return NULL;
            *level_out = level;
            return slot;
        }

        table = TableOf(e);
    }
    return NULL;
}

/**
 * @brief Runs @p fn on every leaf entry under @p table that overlaps [@p va, @p end).
 *
 * Empty entries are skipped whole, so a sparse range costs one step per empty entry at the
 * highest level where it is empty.
 *
 * @param[in,out] table Table at @p level.
 * @param[in]     level Translation level of @p table.
 * @param[in]     va    Start of the range.
 * @param[in]     end   End of the range, exclusive.
 * @param[in]     fn    Callback.
 * @param[in,out] ctx   Passed to @p fn.
 */
static void WalkLevel(AArch64Pte *table, int level, VirtAddr va, VirtAddr end, WalkFn fn, void *ctx)
{
    uint64_t size = PteLevelSize(level);

    while (va < end) {
        AArch64Pte *slot = &table[PteIndex(va, level)];
        AArch64Pte e = *slot;
        VirtAddr next = (va | (size - 1)) + 1;
        VirtAddr stop = (next <= va || next > end) ? end : next;

        if (PteIsTable(e, level))
            WalkLevel(TableOf(e), level + 1, va, stop, fn, ctx);
        else if ((e & PTE_VALID) && level != 0)
            fn(slot, level, va, ctx);

        if (next <= va)
            break;
        va = next;
    }
}

/**
 * @brief Runs @p fn on every page and block entry that overlaps [@p va, @p va + @p size).
 *
 * @param[in,out] root Level 0 table.
 * @param[in]     va   Start of the range.
 * @param[in]     size Size in bytes.
 * @param[in]     fn   Callback.
 * @param[in,out] ctx  Passed to @p fn.
 */
static void WalkRange(AArch64Pte *root, VirtAddr va, size_t size, WalkFn fn, void *ctx)
{
    WalkLevel(root, 0, va, va + size, fn, ctx);
}

/**
 * @brief Frees a table and every table below it, but not the pages or blocks they map.
 *
 * @param[in] table Table at @p level.
 * @param[in] level Translation level of @p table.
 */
static void FreeLevel(AArch64Pte *table, int level)
{
    for (unsigned i = 0; i < PTE_ENTRIES && level < PTE_LEVEL_LAST; i++) {
        if (PteIsTable(table[i], level))
            FreeLevel(TableOf(table[i]), level + 1);
    }
    PmmFreeFrame(VA_TO_PA((VirtAddr)table));
}

PhysAddr ArchMmuCreateTables(AddressSpaceType type)
{
    (void)type;
    PhysAddr pa = AllocTable();
    return pa == PA_NULL ? 0 : pa;
}

void ArchMmuFreeTables(PhysAddr ttbr_pa, AddressSpaceType type)
{
    (void)type;
    if (!ttbr_pa)
        return;
    FreeLevel((AArch64Pte *)PA_TO_VA(ttbr_pa), 0);
}

/**
 * @brief Tells whether a 2 MiB block can map [@p va, @p va + @p left) from @p pa.
 *
 * @param[in] va   Virtual address.
 * @param[in] pa   Physical address.
 * @param[in] left Bytes still to map.
 * @return true if both addresses are 2 MiB-aligned and at least 2 MiB is left.
 */
static bool FitsBlock(VirtAddr va, PhysAddr pa, size_t left)
{
    uint64_t size = PteLevelSize(PTE_LEVEL_BLOCK);
    return !((va | pa) & (size - 1)) && left >= size;
}

/**
 * @brief Maps [va, va+size) to [pa, pa+size) in as, with prot and memtype.
 *
 * @param[in] as The address space to map the range to.
 * @param[in] va The virtual address to map the range to.
 * @param[in] pa The physical address the VA will correspond to.
 * @param[in] size The size of the range.
 * @param[in] prot The protection flags of memory.
 * @param[in] memtype The type of memory the range will be in.
 * @param[in] allow_blocks Allows use of blocks in PTE construction.
 *
 * @return bool
 */
static bool MapRange(AddressSpace *as, VirtAddr va, PhysAddr pa, size_t size, MemProt prot,
                     VirtMemType memtype, bool allow_blocks)
{
    if ((va | pa | size) & (PAGE_SIZE - 1))
        return false;

    AArch64Pte *root = RootOf(as);
    AArch64Pte attrs = ProtToDesc(prot, memtype) | PTE_VALID;
    bool blocks = allow_blocks && as->type != ADDRESS_SPACE_USER;

    for (size_t off = 0; off < size;) {
        if (blocks && FitsBlock(va + off, pa + off, size - off)) {
            AArch64Pte *slot = WalkTo(root, va + off, PTE_LEVEL_BLOCK);
            if (slot && !(*slot & PTE_VALID)) {
                *slot = (pa + off) | attrs;
                off += PteLevelSize(PTE_LEVEL_BLOCK);
                continue;
            }
        }

        AArch64Pte *slot = WalkTo(root, va + off, PTE_LEVEL_LAST);
        if (!slot) {
            ArchMmuUnmap(as, va, off, false);
            return false;
        }
        *slot = (pa + off) | attrs | PTE_TABLE_OR_PAGE;
        off += PAGE_SIZE;
    }
    __asm__ volatile("dsb ishst" ::: "memory");
    return true;
}

bool ArchMmuMap(AddressSpace *as, VirtAddr va, PhysAddr pa, size_t size, MemProt prot,
                VirtMemType memtype)
{
    return MapRange(as, va, pa, size, prot, memtype, true);
}

bool ArchMmuMapPages(AddressSpace *as, VirtAddr va, PhysAddr pa, size_t size, MemProt prot,
                     VirtMemType memtype)
{
    return MapRange(as, va, pa, size, prot, memtype, false);
}

/** @brief Context of ClearSlot(). */
typedef struct {
    VirtAddr end; /**< End of the range being unmapped, exclusive. */
    bool found;   /**< Set when any entry was cleared. */
} ClearCtx;

/**
 * @brief WalkRange() callback for ArchMmuUnmap(): clears pages, and blocks the range covers whole.
 *
 * A block the range only partly covers is left mapped.
 *
 * @param[in,out] slot  Leaf entry.
 * @param[in]     level Its level.
 * @param[in]     va    Where the range enters the entry.
 * @param[in,out] ctx   ClearCtx.
 */
static void ClearSlot(AArch64Pte *slot, int level, VirtAddr va, void *ctx)
{
    ClearCtx *c = ctx;
    uint64_t size = PteLevelSize(level);

    if (level != PTE_LEVEL_LAST && ((va & (size - 1)) || c->end - va < size))
        return;
    *slot = 0;
    c->found = true;
}

bool ArchMmuUnmap(AddressSpace *as, VirtAddr va, size_t size, bool flush)
{
    ClearCtx c = {.end = va + size, .found = false};

    WalkRange(RootOf(as), va, size, ClearSlot, &c);
    __asm__ volatile("dsb ishst" ::: "memory");
    if (flush && c.found)
        ArchMmuFlushTlbAsid(as->asid_token.asid);
    return c.found;
}

/** @brief Context of ProtectSlot(). */
typedef struct {
    MemProt prot; /**< New protection. */
    bool changed; /**< Set when any entry was rewritten. */
} ProtectCtx;

/**
 * @brief WalkRange() callback for ArchMmuProtect(): rewrites only the permission bits.
 *
 * Address, memory type, shareability and access flag stay, so the live entry can be rewritten
 * in one store without break-before-make. Memory that is not Normal stays non-executable.
 *
 * @param[in,out] slot  Leaf entry, page or block.
 * @param[in]     level Unused.
 * @param[in]     va    Unused.
 * @param[in,out] ctx   ProtectCtx.
 */
static void ProtectSlot(AArch64Pte *slot, int level, VirtAddr va, void *ctx)
{
    ProtectCtx *c = ctx;
    AArch64Pte e = *slot;

    (void)level;
    (void)va;

    e &= ~PTE_PERM_MASK;
    e |= ProtToDesc(c->prot, VM_MEM_NORMAL) & PTE_PERM_MASK;
    if ((e & PTE_ATTRINDX_MASK) != PTE_ATTRINDX(PTE_ATTRINDX_NORMAL))
        e |= PTE_PXN | PTE_UXN;

    *slot = e;
    c->changed = true;
}

bool ArchMmuProtect(AddressSpace *as, VirtAddr va, size_t size, MemProt prot)
{
    ProtectCtx c = {.prot = prot, .changed = false};

    WalkRange(RootOf(as), va, size, ProtectSlot, &c);
    __asm__ volatile("dsb ishst" ::: "memory");
    if (c.changed)
        ArchMmuFlushTlbAsid(as->asid_token.asid);
    return c.changed;
}

/**
 * @brief Empty L0 table to switch to when the idle task runs, so TTBR0 doesn't receive the jernelt ables.
 */
static AArch64Pte empty_l0[PTE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));

void ArchMmuSwitch(AddressSpace *as)
{
    if (!as || as->pt_root_physaddr == 0)
        return;

    if (as->type == ADDRESS_SPACE_KERNEL) {
        WRITE_SYSREG(ttbr0_el1, VA_TO_PA((VirtAddr)empty_l0));
        Isb();
        AsidSetActive(0);
        return;
    }

    if (as->asid_token.generation != AsidCurrentGeneration()) {
        AsidFree(as->asid_token);
        as->asid_token = AsidAlloc();
    }
    WRITE_SYSREG(ttbr0_el1, as->pt_root_physaddr | ((uint64_t)as->asid_token.asid << 48));
    Isb();
    AsidSetActive(as->asid_token.asid);
}

void ArchMmuEnable(AddressSpace *as)
{
    if (!as || as->pt_root_physaddr == 0)
        return;
    ArchMmuSwitch(as);
    ArchMmuFlushTlb();
}

void ArchMmuFlushTlb(void)
{
    __asm__ volatile("dsb ishst\n"
                     "tlbi vmalle1\n"
                     "dsb ish\n"
                     "isb\n" ::
                         : "memory");
}

void ArchMmuFlushTlbAsid(Asid asid)
{
    if (asid == 0) {
        ArchMmuFlushTlb();
        return;
    }

    __asm__ volatile("dsb ishst\n"
                     "tlbi aside1, %0\n"
                     "dsb ish\n"
                     "isb\n" ::"r"(TLBI_ASID(asid))
                     : "memory");
}

void ArchMmuFlushTlbVa(VirtAddr va)
{
    __asm__ volatile("dsb ishst\n"
                     "tlbi vaae1, %0\n"
                     "dsb ish\n"
                     "isb\n" ::"r"(TLBI_VA(va))
                     : "memory");
}

void ArchMmuFlushTlbVaAsid(VirtAddr va, Asid asid)
{
    if (asid == 0) {
        ArchMmuFlushTlbVa(va);
        return;
    }

    __asm__ volatile("dsb ishst\n"
                     "tlbi vae1, %0\n"
                     "dsb ish\n"
                     "isb\n" ::"r"(TLBI_ASID(asid) | TLBI_VA(va))
                     : "memory");
}

PhysAddr ArchMmuTranslate(PhysAddr ttbr_pa, VirtAddr va)
{
    int level;
    AArch64Pte *slot = Lookup((AArch64Pte *)PA_TO_VA(ttbr_pa), va, &level);
    if (!slot)
        return 0;

    uint64_t size = PteLevelSize(level);
    return (PhysAddr)((*slot & PTE_ADDR_MASK & ~(size - 1)) | (va & (size - 1)));
}

bool ArchMmuUnmapPage(AddressSpace *as, VirtAddr va)
{
    int level;
    AArch64Pte *slot = Lookup(RootOf(as), va, &level);
    if (!slot || level != PTE_LEVEL_LAST)
        return false;

    *slot = 0;
    return true;
}

/** @brief Walks the sorted region list alongside an ascending scan of user addresses. */
typedef struct {
    const AddressSpace *as; /**< Address space being torn down. */
    uint32_t idx;           /**< First region that may still contain the next address. */
} RegionCursor;

/**
 * @brief Finds what backs a user address, for addresses passed in ascending order.
 *
 * @param[in,out] rc Cursor; advanced past regions that end at or below @p va.
 * @param[in]     va Virtual address, not below the previous call's.
 * @return The backing of the region that contains @p va, or VM_BACKING_ANON if none does.
 */
static VirtMemBacking RegionCursorOwner(RegionCursor *rc, VirtAddr va)
{
    while (rc->idx < rc->as->regions.len) {
        const VirtMemRegion *r = &rc->as->regions.data[rc->idx];
        VirtAddr end = r->vaddr_start + r->size;
        if (end >= r->vaddr_start && va < end)
            return va >= r->vaddr_start ? r->owner : VM_BACKING_ANON;
        rc->idx++;
    }
    return VM_BACKING_ANON;
}

/**
 * @brief Frees a user table, every table below it, and the anonymous pages they map.
 *
 * Skips blocks, the syspage, device memory, pages owned by memory objects and pages at or above
 * USER_VA_TOP. Runs of eight empty entries are skipped with one test.
 *
 * @param[in]     table Table at @p level.
 * @param[in]     level Translation level of @p table.
 * @param[in]     base  Virtual address the table's first entry maps.
 * @param[in,out] rc    Region cursor.
 */
static void FreeUserLevel(AArch64Pte *table, int level, VirtAddr base, RegionCursor *rc)
{
    uint64_t size = PteLevelSize(level);

    for (unsigned g = 0; g < PTE_ENTRIES; g += 8) {
        const AArch64Pte *p = &table[g];
        if (!(p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7]))
            continue;
        for (unsigned i = g; i < g + 8; i++) {
            AArch64Pte e = table[i];
            VirtAddr va = base + (VirtAddr)i * size;

            if (!(e & PTE_VALID))
                continue;
            if (PteIsTable(e, level)) {
                FreeUserLevel(TableOf(e), level + 1, va, rc);
                continue;
            }
            if (level != PTE_LEVEL_LAST || va >= USER_VA_TOP || va == USER_SYSPAGE_VA)
                continue;
            if ((e & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(PTE_ATTRINDX_DEVICE))
                continue;
            if (RegionCursorOwner(rc, va) != VM_BACKING_ANON)
                continue;
            PmmFreeFrame(e & PTE_ADDR_MASK);
        }
    }
    PmmFreeFrame(VA_TO_PA((VirtAddr)table));
}

void ArchMmuFreeUserPages(AddressSpace *as)
{
    if (!as || !as->pt_root_physaddr)
        return;

    if (as->asid_token.asid != 0)
        ArchMmuFlushTlbAsid(as->asid_token.asid);

    RegionCursor rc = {.as = as, .idx = 0};
    FreeUserLevel(RootOf(as), 0, 0, &rc);
    as->pt_root_physaddr = 0;
}

void ArchMmuInitTtbr1(AddressSpace *as)
{
    __asm__ volatile("dsb ishst" ::: "memory");
    WRITE_SYSREG(ttbr1_el1, as->pt_root_physaddr);
    Isb();
    ArchMmuFlushTlb();
}

bool ArchMmuInstallKernelSpace(AddressSpace *as)
{
    size_t periph_size = (size_t)BOARD_PERIPH_MB << 20;

    if (!ArchMmuMap(as, PA_TO_VA(BOARD_PERIPH_PA_BASE), BOARD_PERIPH_PA_BASE, periph_size,
                    PROT_READ | PROT_WRITE, VM_MEM_DEVICE))
        return false;

    ArchMmuInitTtbr1(as);
    return true;
}

void ArchMmuDropIdentity(AddressSpace *as)
{
    ArchMmuSwitch(as);
    ArchMmuFlushTlb();
}
