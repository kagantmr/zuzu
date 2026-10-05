#include "pmm_internal.h"

#include "core/panic.h"

#include "kernel/bench.h"
#include "kernel/dev/fdt_wrappers.h"
#include "kernel/layout.h"
#include "kernel/mm/vmm/vmm.h" // PA_TO_VA / VA_TO_PA helpers
#include "zuzu/err.h"
#include <arch/symbols.h>

#include <assert.h>
#include <list.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <types.h>

#define LOG_FMT(fmt) "(pmm) " fmt
#include "util/log.h"

PmmState pmm_state;
extern RamLayout kernel_layout;
extern void SyspageUpdateMem(void);

typedef struct { PhysAddr next; PhysAddr prev; } FreeNode;
static inline FreeNode *Node(PhysAddr pa) { return (FreeNode *)PA_TO_VA(pa); }

static inline void PushNode(PhysAddr pa)
{
    FreeNode *node = Node(pa);
    node->next = pmm_state.freelist_head;
    node->prev = 0;
    if (pmm_state.freelist_head)
        Node(pmm_state.freelist_head)->prev = pa;
    pmm_state.freelist_head = pa;
}

static inline PhysAddr PopNode(void)
{
    PhysAddr pa = pmm_state.freelist_head;
    if (!pa)
        return 0;
    PhysAddr next = Node(pa)->next;
    pmm_state.freelist_head = next;
    if (next)
        Node(next)->prev = 0;
    return pa;
}

static inline void UnlinkNode(PhysAddr pa)
{
    PhysAddr prev = Node(pa)->prev;
    PhysAddr next = Node(pa)->next;
    if (prev) Node(prev)->next = next; else pmm_state.freelist_head = next;
    if (next) Node(next)->prev = prev;
}

static bool PmmIsRecorded(PhysAddr pa)
{
    if (pa == 0)
        return true; // freelist terminator
    if ((pa % PAGE_SIZE) != 0)
        return false;

    const Pfn pfn = PaToPfn(pa);
    return (pfn >= pmm_state.pfn_base && pfn < pmm_state.pfn_end);
}

static void PmmRebuildFreelist(void)
{
    pmm_state.freelist_head = 0;
    for (size_t i = 0; i < pmm_state.total_frames; i++) {
        size_t byte_idx = i / 8;
        size_t bit_idx = i % 8;
        if (!(pmm_state.bitmap[byte_idx] & (1U << bit_idx))) {
            PhysAddr pa = PfnToPa(pmm_state.pfn_base + i);
            PushNode(pa);
        }
    }
    pmm_state.freelist_ready = true;
}

static PhysAddr PmmTakeFreeFrame(void)
{
    if (pmm_state.freelist_head == 0)
        return (PhysAddr)0;

    if (!PmmIsRecorded(pmm_state.freelist_head)) {
        PmmRebuildFreelist();
        if (pmm_state.freelist_head == 0)
            return (PhysAddr)0;
    }

    if (!PmmIsRecorded(Node(pmm_state.freelist_head)->next)) {
        PmmRebuildFreelist();
        if (pmm_state.freelist_head == 0)
            return (PhysAddr)0;
        if (!PmmIsRecorded(Node(pmm_state.freelist_head)->next)) {
            pmm_state.freelist_head = 0;
            return (PhysAddr)0;
        }
    }

    PhysAddr pa = PopNode();

    /* Keep bitmap in sync */
    size_t index = PaToPfn(pa) - pmm_state.pfn_base;
    size_t byte_idx = index / 8;
    size_t bit_idx = index % 8;

    assert(byte_idx < pmm_state.bitmap_bytes);
    assert(!(pmm_state.bitmap[byte_idx] & (1U << bit_idx))); /* must be free in bitmap */

    pmm_state.bitmap[byte_idx] |= (uint8_t)(1U << bit_idx);
    pmm_state.free_frames--;
    assert(pmm_state.free_frames <= pmm_state.total_frames);

    PmmSignalSubscribers();
    return pa;
}

static void PmmReserveBootRegions(void)
{
    PmmMarkRange((PhysAddr)_boot_start, (PhysAddr)_boot_end);
    /* The DTB is wherever the bootloader put it, not necessarily adjacent
     * to the kernel (QEMU's Linux-boot path and the Pi firmware both place
     * it independently). Reserve its actual extent. */
    PmmMarkRange(kernel_layout.dtb_start_pa, kernel_layout.dtb_start_pa + FdtTotalSize());
    PmmMarkRange(kernel_layout.kernel_start_pa, kernel_layout.kernel_end_pa);
    PmmMarkRange(kernel_layout.bitmap_start_pa, kernel_layout.bitmap_end_pa);

    // All mode stacks
    PmmMarkRange((PhysAddr)__stack_region_base__, (PhysAddr)__stack_region_end__);

    /* Firmware /memreserve/ ranges (e.g. secondary-core spin tables on the
     * Pi 4). Ranges outside managed RAM are rejected by PmmMarkRange. */
    uint64_t rsv_addr, rsv_size;
    for (uint32_t i = 0; FdtGetReservedMem(i, &rsv_addr, &rsv_size); i++)
        PmmMarkRange((PhysAddr)rsv_addr, (PhysAddr)(rsv_addr + rsv_size));

    /* A bootloader-supplied initrd (DTB /chosen) lives outside the kernel
     * image, wherever it was loaded, so it needs its own reservation. */
    uint64_t initrd_start, initrd_end;
    if (FdtGetInitrd(&initrd_start, &initrd_end))
        PmmMarkRange((PhysAddr)initrd_start, (PhysAddr)initrd_end);
}

void PmmInit(void)
{
    // Compute PFN range from phys_region
    pmm_state.pfn_base = PaToPfn(kernel_layout.ram_start);
    pmm_state.pfn_end = PaToPfn(kernel_layout.ram_end);
    pmm_state.total_frames = pmm_state.pfn_end - pmm_state.pfn_base;
    pmm_state.free_frames = pmm_state.total_frames;

    // Place bitmap after kernel, page-aligned
    PhysAddr bitmap_start_pa = align_up(kernel_layout.kernel_end_pa, PAGE_SIZE);
    size_t bitmap_bytes = (pmm_state.total_frames + 7) / 8;
    size_t bitmap_size = align_up(bitmap_bytes, PAGE_SIZE);
    PhysAddr bitmap_end_pa = bitmap_start_pa + bitmap_size;

    // Sanity checks
    assert(bitmap_end_pa <= kernel_layout.stack_base_pa);
    assert(bitmap_end_pa <= kernel_layout.ram_end);

    // Record in layout (physical placement)
    kernel_layout.bitmap_start_pa = bitmap_start_pa;
    kernel_layout.bitmap_end_pa = bitmap_end_pa;

    // Establish dereferenceable VA for the bitmap.
    // After identity mapping is removed, the bitmap MUST be accessed via VA.
    kernel_layout.bitmap_va = (uint8_t *)PA_TO_VA(kernel_layout.bitmap_start_pa);

    // Install and zero (use VA pointer)
    pmm_state.bitmap = kernel_layout.bitmap_va;
    pmm_state.bitmap_bytes = bitmap_bytes;
    memset(pmm_state.bitmap, 0, bitmap_size);

    // Reserve boot-time regions
    PmmReserveBootRegions();

    // Build the freelist from all free pages in the bitmap
    PmmRebuildFreelist();

    ListInit(&pmm_subscribers);
}

PmmStats PmmGetStats(void)
{
    return (PmmStats){.total_frames = pmm_state.total_frames, .free_frames = pmm_state.free_frames};
}

/* mark: mark pages in [start, end) as USED */
Err PmmMarkRange(PhysAddr start, PhysAddr end)
{
    if (start >= end)
        return ERR_BADARG;

    /* Align the range to page boundaries */
    PhysAddr astart = align_down(start, PAGE_SIZE);
    PhysAddr aend = align_up(end, PAGE_SIZE);

    Pfn start_pfn = PaToPfn(astart);
    Pfn end_pfn = PaToPfn(aend);

    /* PFN bounds check (pfn_end is exclusive) */
    if (start_pfn < pmm_state.pfn_base || end_pfn > pmm_state.pfn_end) {
        return ERR_BADARG;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.pfn_end > pmm_state.pfn_base);
    assert(pmm_state.total_frames == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_frames);

    bool needs_rebuild = false;
    for (Pfn pfn = start_pfn; pfn < end_pfn; pfn++) {
        size_t index = pfn - pmm_state.pfn_base;
        size_t byte_idx = index / 8;
        size_t bit_idx = index % 8;

        /* safety: ensure we do not walk past bitmap */
        assert(byte_idx < pmm_state.bitmap_bytes);
        if (byte_idx >= pmm_state.bitmap_bytes)
            break;

        uint8_t mask = (uint8_t)(1U << bit_idx);

        /* Only flip and update counters if bit was previously 0 */
        if (!(pmm_state.bitmap[byte_idx] & mask)) {
            pmm_state.bitmap[byte_idx] |= mask;
            if (pmm_state.free_frames > 0)
                pmm_state.free_frames--;
            assert(pmm_state.free_frames <= pmm_state.total_frames);

            if (pmm_state.freelist_ready) {
                const FreeNode *node = Node(PfnToPa(pfn));
                if (PmmIsRecorded(node->prev) && PmmIsRecorded(node->next))
                    UnlinkNode(PfnToPa(pfn));
                else
                    needs_rebuild = true;
            }
        }
    }

    if (needs_rebuild)
        PmmRebuildFreelist();

    return ZUZU_OK;
}

/* unmark: mark pages in [start, end) as FREE */
Err PmmUnmarkRange(const PhysAddr start, const PhysAddr end)
{
    if (start >= end)
        return ERR_BADARG;

    const PhysAddr astart = align_down(start, PAGE_SIZE);
    const PhysAddr aend = align_up(end, PAGE_SIZE);

    const Pfn start_pfn = PaToPfn(astart);
    Pfn end_pfn = PaToPfn(aend);

    if (start_pfn < pmm_state.pfn_base || end_pfn > pmm_state.pfn_end) {
        return ERR_BADARG;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.pfn_end > pmm_state.pfn_base);
    assert(pmm_state.total_frames == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_frames);

    for (Pfn pfn = start_pfn; pfn < end_pfn; pfn++) {
        const size_t index = pfn - pmm_state.pfn_base;
        const size_t byte_idx = index / 8;
        const size_t bit_idx = index % 8;

        assert(byte_idx < pmm_state.bitmap_bytes);
        if (byte_idx >= pmm_state.bitmap_bytes)
            break;

        const uint8_t mask = (uint8_t)(1U << bit_idx);

        /* Only flip and update counters if bit was previously 1 */
        if (pmm_state.bitmap[byte_idx] & mask) {
            pmm_state.bitmap[byte_idx] &= ~mask;
            if (pmm_state.free_frames < pmm_state.total_frames)
                pmm_state.free_frames++;
            assert(pmm_state.free_frames <= pmm_state.total_frames);

            if (pmm_state.freelist_ready)
                PushNode(PfnToPa(pfn));
        }
    }

    return ZUZU_OK;
}

PhysAddr PmmAllocFrame(void)
{
    PhysAddr pa = PmmTakeFreeFrame();
    if (pa != 0) {
        SyspageUpdateMem();
    }
    return pa;
}
PhysAddr PmmAllocFramesContig(size_t n_frames)
{

    if (n_frames == 0 || pmm_state.free_frames < n_frames) {
        return PA_NULL;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.total_frames == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_frames);
    assert(pmm_state.free_frames <= pmm_state.total_frames);
    assert(n_frames <= pmm_state.total_frames);

    size_t total_pages = pmm_state.total_frames;
    size_t consecutive = 0;
    size_t start_index = 0;

    for (size_t index = 0; index < total_pages; index++) {
        size_t byte_idx = index / 8;
        size_t bit_idx = index % 8;
        uint8_t mask = (uint8_t)(1U << bit_idx);

        if (byte_idx >= pmm_state.bitmap_bytes) {
            break; /* beyond managed pages */
        }

        if (!(pmm_state.bitmap[byte_idx] & mask)) { /* free */
            if (consecutive == 0) {
                start_index = index;
            }
            consecutive++;

            if (consecutive == n_frames) {
                /* Mark pages as allocated */
                PhysAddr start_pa = PfnToPa(pmm_state.pfn_base + start_index);
                PhysAddr end_pa = PfnToPa(pmm_state.pfn_base + start_index + n_frames);
                if (PmmMarkRange(start_pa, end_pa) != ZUZU_OK) {
                    return PA_NULL; /* marking failed */
                }

                Pfn pfn = pmm_state.pfn_base + start_index;
                PhysAddr addr = PfnToPa(pfn);
                assert(addr % PAGE_SIZE == 0);
                assert(pfn >= pmm_state.pfn_base && (pfn + n_frames) <= pmm_state.pfn_end);
                SyspageUpdateMem(); // update free memory info in syspage
                PmmSignalSubscribers();
                return addr;
            }
        } else {
            consecutive = 0; /* reset */
        }
    }

    return PA_NULL;
}

void PmmFreeFrame(const PhysAddr addr)
{
    assert(addr % PAGE_SIZE == 0);

    const Pfn pfn = PaToPfn(addr);

    /* bounds: pfn must be inside [pfn_base, pfn_end) */
    assert(pfn >= pmm_state.pfn_base && pfn < pmm_state.pfn_end);

    size_t index = pfn - pmm_state.pfn_base;
    size_t byte_idx = index / 8;
    size_t bit_idx = index % 8;

    assert(byte_idx < pmm_state.bitmap_bytes);

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.free_frames <= pmm_state.total_frames);

    const uint8_t mask = (uint8_t)(1U << bit_idx);

    /* if bit set -> allocated -> free it */
    if (pmm_state.bitmap[byte_idx] & mask) {
        pmm_state.bitmap[byte_idx] &= ~mask;
        pmm_state.free_frames++;
        assert(pmm_state.free_frames <= pmm_state.total_frames);

        /* Push onto freelist */
        PushNode(addr);

        SyspageUpdateMem();
        return;
    }

    /* already free -> double free: a live bug, not a bad argument */
    panic("pmm: double free of frame %x", (unsigned int)pfn);
}

#ifdef CONFIG_ZUZU_BENCH
BENCH_STAT(g_bench_pmm_scan, "pmm contig: bitmap scan");
BENCH_STAT(g_bench_pmm_freelist, "pmm contig: mark (unlinks)");
BENCH_STAT(g_bench_pmm_tail, "pmm contig: syspage+signal");
#endif

PhysAddr PmmAllocFramesContigAligned(const size_t n_frames, size_t align_frames)
{

    if (n_frames == 0) {
        return PA_NULL;
    }
    if (align_frames == 0)
        align_frames = 1;
    // Require power-of-two alignment (common + cheap)
    if ((align_frames & (align_frames - 1)) != 0) {
        return PA_NULL;
    }

    if (pmm_state.free_frames < n_frames) {
        return PA_NULL;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.total_frames == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_frames);

    const size_t total_pages = pmm_state.total_frames;
    size_t consecutive = 0;
    size_t start_index = 0;
#ifdef CONFIG_ZUZU_BENCH
    uint32_t bench_scan_start = BENCH_BEGIN();
#endif

    for (size_t index = 0; index < total_pages; index++) {
        // Enforce alignment on the start of a run
        if (consecutive == 0) {
            if (((pmm_state.pfn_base + index) & (align_frames - 1)) != 0) {
                continue;
            }
        }

        size_t byte_idx = index / 8;
        size_t bit_idx = index % 8;
        uint8_t mask = (uint8_t)(1U << bit_idx);

        if (byte_idx >= pmm_state.bitmap_bytes)
            break;

        if (!(pmm_state.bitmap[byte_idx] & mask)) { // free
            if (consecutive == 0)
                start_index = index;
            consecutive++;

            if (consecutive == n_frames) {
                const uintptr_t start_pa = PfnToPa(pmm_state.pfn_base + start_index);
                const uintptr_t end_pa = PfnToPa(pmm_state.pfn_base + start_index + n_frames);

#ifdef CONFIG_ZUZU_BENCH
                BENCH_END(g_bench_pmm_scan, bench_scan_start);
                uint32_t bench_step = BENCH_BEGIN();
#endif
                if (PmmMarkRange(start_pa, end_pa) != ZUZU_OK) {
                    return PA_NULL;
                }

#ifdef CONFIG_ZUZU_BENCH
                BENCH_END(g_bench_pmm_freelist, bench_step);
                bench_step = BENCH_BEGIN();
#endif

                SyspageUpdateMem(); // update free memory info in syspage
                PmmSignalSubscribers();
#ifdef CONFIG_ZUZU_BENCH
                BENCH_END(g_bench_pmm_tail, bench_step);
#endif

                return start_pa;
            }
        } else {
            consecutive = 0;
        }
    }

    return PA_NULL;
}

size_t PmmAllocFramesScattered(const size_t n_frames, PhysAddr *out_addrs)
{
    if (n_frames == 0 || !out_addrs || pmm_state.free_frames < n_frames) {
        return 0;
    }
    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.total_frames == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_frames);
    assert(pmm_state.free_frames <= pmm_state.total_frames);
    assert(n_frames <= pmm_state.total_frames);
    for (size_t i = 0; i < n_frames; i++) {
        const PhysAddr new_page = PmmTakeFreeFrame();
        if (new_page == 0) {
            SyspageUpdateMem();
            return i;
        }
        out_addrs[i] = new_page;
    }
    SyspageUpdateMem(); // update free memory info in syspage
    return n_frames;
}
