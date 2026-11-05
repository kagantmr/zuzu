/**
 * @file mmu.c
 * @brief AArch64 translation tables: creating, mapping, protecting and tearing down, and TLB
 * maintenance.
 */

#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
#include <arch/mmu.h>
#include <arch_impl/aarch64_mmu.h>
#include <string.h>
#include <arch/asid.h>
#include <zuzu/user_layout.h>

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

uintptr_t ArchMmuCreateTables(AddressSpaceType type)
{
    (void)type;
    PhysAddr pa = AllocTable();
    return pa == PA_NULL ? 0 : (uintptr_t)pa;
}

void ArchMmuFreeTables(uintptr_t ttbr_pa, AddressSpaceType type)
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

bool ArchMmuMap(AddressSpace *as, uintptr_t va, uintptr_t pa, size_t size, MemProt prot,
                VirtMemType memtype)
{
    if ((va | pa | size) & (PAGE_SIZE - 1))
        return false;

    AArch64Pte *root = RootOf(as);
    AArch64Pte attrs = ProtToDesc(prot, memtype) | PTE_VALID;
    bool blocks = !(prot & VM_PROT_USER);

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

bool ArchMmuUnmap(AddressSpace *as, uintptr_t va, size_t size, bool flush)
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

bool ArchMmuProtect(AddressSpace *as, uintptr_t va, size_t size, MemProt prot)
{
    ProtectCtx c = {.prot = prot, .changed = false};

    WalkRange(RootOf(as), va, size, ProtectSlot, &c);
    __asm__ volatile("dsb ishst" ::: "memory");
    if (c.changed)
        ArchMmuFlushTlbAsid(as->asid_token.asid);
    return c.changed;
}

void ArchMmuSwitch(AddressSpace *as)
{
    if (!as || as->pt_root_physaddr == 0) return;
    if (as->asid_token.generation != AsidCurrentGeneration()) {
        AsidFree(as->asid_token);
        as->asid_token = AsidAlloc();
    }
    uint64_t ttbr = as->pt_root_physaddr | ((uint64_t)as->asid_token.asid << 48);
    __asm__ volatile(
        "msr ttbr0_el1, %0\n"
        "isb\n"
        :: "r"(ttbr) : "memory");
    AsidSetActive(as->asid_token.asid);
}

void ArchMmuEnable(AddressSpace *as) {
    if (!as || as->pt_root_physaddr == 0) return;
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

void ArchMmuFlushTlbVa(uintptr_t va)
{
    __asm__ volatile("dsb ishst\n"
                     "tlbi vaae1, %0\n"
                     "dsb ish\n"
                     "isb\n" ::"r"(TLBI_VA(va))
                     : "memory");
}

void ArchMmuFlushTlbVaAsid(uintptr_t va, Asid asid)
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

uintptr_t ArchMmuTranslate(uintptr_t ttbr_pa, uintptr_t va)
{
    int level;
    AArch64Pte *slot = Lookup((AArch64Pte *)PA_TO_VA(ttbr_pa), va, &level);
    if (!slot)
        return 0;

    uint64_t size = PteLevelSize(level);
    return (uintptr_t)((*slot & PTE_ADDR_MASK & ~(size - 1)) | (va & (size - 1)));
}

bool ArchMmuUnmapPage(AddressSpace *as, uintptr_t va)
{
    int level;
    AArch64Pte *slot = Lookup(RootOf(as), va, &level);
    if (!slot || level != PTE_LEVEL_LAST)
        return false;

    *slot = 0;
    return true;
}

/**
 * @brief Finds what backs a user address.
 *
 * @param[in] as Address space.
 * @param[in] va Virtual address.
 * @return The backing of the region that contains @p va, or VM_BACKING_ANON if none does.
 */
static VirtMemBacking RegionOwnerForVa(const AddressSpace *as, VirtAddr va)
{
    for (uint32_t i = 0; i < as->regions.len; i++) {
        const VirtMemRegion *r = vm_region_vec_get_const(&as->regions, i);
        if (!r)
            continue;

        VirtAddr start = r->vaddr_start;
        VirtAddr end = start + r->size;
        if (end < start)
            continue;

        if (va >= start && va < end)
            return r->owner;
    }

    return VM_BACKING_ANON;
}

/**
 * @brief WalkRange() callback for ArchMmuFreeUserPages(): frees anonymous pages' frames.
 *
 * Skips blocks, the syspage, device memory and pages owned by memory objects. The entry is not
 * cleared, since the tables are freed right after.
 *
 * @param[in] slot  Leaf entry.
 * @param[in] level Its level.
 * @param[in] va    Address of the page.
 * @param[in] ctx   The AddressSpace being torn down.
 */
static void FreeAnonPage(AArch64Pte *slot, int level, VirtAddr va, void *ctx)
{
    AArch64Pte e = *slot;

    if (level != PTE_LEVEL_LAST || va == USER_SYSPAGE_VA)
        return;
    if ((e & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(PTE_ATTRINDX_DEVICE))
        return;
    if (RegionOwnerForVa((const AddressSpace *)ctx, va) != VM_BACKING_ANON)
        return;

    PmmFreeFrame(e & PTE_ADDR_MASK);
}

void ArchMmuFreeUserPages(AddressSpace *as)
{
    if (!as)
        return;

    WalkRange(RootOf(as), 0, USER_VA_TOP, FreeAnonPage, as);
}

void ArchMmuInitTtbr1(AddressSpace *as)
{
    __asm__ volatile("dsb ishst\n"
                     "msr ttbr1_el1, %0\n"
                     "isb\n"
                     "tlbi vmalle1\n"
                     "dsb ish\n"
                     "isb\n" ::"r"(as->pt_root_physaddr)
                     : "memory");
}
