#include "kernel/mm/pmm.h"

#include "core/panic.h"

#include "kernel/ipc/ntfn.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/vmm.h" // PA_TO_VA / VA_TO_PA helpers
#include "kernel/layout.h"
#include "kernel/dev/fdt_wrappers.h"
#include "zuzu/err.h"
#include "zuzu/event.h"
#include <arch/symbols.h>

#include <list.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <spinlock.h>

#include <zuzu/types.h>

#define LOW_WATER_PCT 20  // fire when free < 20%
#define HIGH_WATER_PCT 30 // clear when free > 30%

#ifdef PMM_TRACE
#include <core/ksym.h>

/* Attributes a trace line to the process making the call. Declared here
 * rather than pulled in via thread.h/process.h to keep pmm.c out of that
 * dependency chain; see kernel/sched/sched.c. */
extern uint32_t current_pid_or_zero(void);
#endif

#define LOG_FMT(fmt) "(pmm) " fmt
#include "zuzu/log.h"

typedef struct {
    Pfn pfn_base;           // lowest page frame number
    Pfn pfn_end;            // highest page frame number (exclusive)
    size_t total_frames;    // total number of pages
    size_t free_frames;     // updated at runtime
    uint8_t *bitmap;        // pointer to bitmap memory
    size_t bitmap_bytes;    // size of bitmap in bytes
    PhysAddr freelist_head; // PA of first free page (or 0 if none)
    bool in_pressure;       // notify if memory is going low to signal via KEvent
} PmmState;

static PmmState pmm_state;
extern kernel_layout_t kernel_layout;
extern void SyspageUpdateMem(void);

typedef struct {
    ListNode node;
    NtfnObj *ntfn;
} PmmSubscriber;

static ListHead pmm_subscribers;

static bool PmmIsRecorded(PhysAddr pa)
{
    if (pa == 0)
        return true; // freelist terminator
    if ((pa % PAGE_SIZE) != 0)
        return false;

    const Pfn pfn = PhysToPfn(pa);
    return (pfn >= pmm_state.pfn_base && pfn < pmm_state.pfn_end);
}

static void PmmRebuildFreelist(void)
{
    pmm_state.freelist_head = 0;
    for (size_t i = 0; i < pmm_state.total_frames; i++) {
        size_t byte_idx = i / 8;
        size_t bit_idx = i % 8;
        if (!(pmm_state.bitmap[byte_idx] & (1U << bit_idx))) {
            PhysAddr pa = PfnToPhys(pmm_state.pfn_base + i);
            PhysAddr *page_va = (PhysAddr *)PA_TO_VA(pa);
            *page_va = pmm_state.freelist_head;
            pmm_state.freelist_head = pa;
        }
    }
}

/* Remove any free-list nodes whose PA is within [start_pa, end_pa). */
static void PmmFreelistRemoveRange(PhysAddr start_pa, PhysAddr end_pa)
{
    PhysAddr prev_pa = 0;
    PhysAddr curr_pa = pmm_state.freelist_head;

    while (curr_pa) {
        if (!PmmIsRecorded(curr_pa)) {
            PmmRebuildFreelist();
            prev_pa = 0;
            curr_pa = pmm_state.freelist_head;
            continue;
        }

        PhysAddr next_pa = *(PhysAddr *)PA_TO_VA(curr_pa);
        ;

        if (!PmmIsRecorded(next_pa)) {
            PmmRebuildFreelist();
            prev_pa = 0;
            curr_pa = pmm_state.freelist_head;
            continue;
        }

        if (curr_pa >= start_pa && curr_pa < end_pa) {
            if (prev_pa == 0) {
                pmm_state.freelist_head = next_pa;
            } else {
                *(PhysAddr *)PA_TO_VA(prev_pa) = next_pa;
            }
        } else {
            prev_pa = curr_pa;
        }

        curr_pa = next_pa;
    }
}


static void PmmKEventSignalUnderLock(void)
{
    size_t free_pct = (pmm_state.free_frames * 100) / pmm_state.total_frames;

    if (!pmm_state.in_pressure && free_pct < LOW_WATER_PCT) {
        pmm_state.in_pressure = true;
        KWARN("Memory usage exceeded low-water mark, signalling subscribers");

        // walk subscribers and signal them
        // clean dead ntfns: refcount--, free-if-zero, kfree(subscriber)
        ListNode *pos, *tmp;
        list_for_each_safe(pos, tmp, &pmm_subscribers.node)
        {
            PmmSubscriber *sub = container_of(pos, PmmSubscriber, node);
            // safe to remove sub from list here
            if (!sub->ntfn->alive) {
                list_remove(pos);
                NtfnRefDrop(sub->ntfn);
                KFree(sub);
                continue;
            }

            NtfnSignal(sub->ntfn, KEVENT_MEMMGMT_BIT);
        }
    } else if (pmm_state.in_pressure && free_pct > HIGH_WATER_PCT) {
        pmm_state.in_pressure = false;
    }
}

static PhysAddr PmmAllocFrameUnderLock(void)
{
    if (pmm_state.freelist_head == 0)
        return (PhysAddr)0;

    if (!PmmIsRecorded(pmm_state.freelist_head)) {
        PmmRebuildFreelist();
        if (pmm_state.freelist_head == 0)
            return (PhysAddr)0;
    }

    PhysAddr pa = pmm_state.freelist_head;

    /* Pop: read next pointer stored in the page itself */
    PhysAddr *page_va = (PhysAddr *)PA_TO_VA(pa);
    PhysAddr next_pa = *page_va;
    if (!PmmIsRecorded(next_pa)) {
        PmmRebuildFreelist();
        if (pmm_state.freelist_head == 0)
            return (PhysAddr)0;
        pa = pmm_state.freelist_head;
        page_va = (PhysAddr *)PA_TO_VA(pa);
        next_pa = *page_va;
        if (!PmmIsRecorded(next_pa)) {
            pmm_state.freelist_head = 0;
            return (PhysAddr)0;
        }
    }

    pmm_state.freelist_head = next_pa;

    /* Keep bitmap in sync */
    size_t index = PhysToPfn(pa) - pmm_state.pfn_base;
    size_t byte_idx = index / 8;
    size_t bit_idx = index % 8;

    assert(byte_idx < pmm_state.bitmap_bytes);
    assert(!(pmm_state.bitmap[byte_idx] & (1U << bit_idx))); /* must be free in bitmap */

    pmm_state.bitmap[byte_idx] |= (uint8_t)(1U << bit_idx);
    pmm_state.free_frames--;
    assert(pmm_state.free_frames <= pmm_state.total_frames);

    PmmKEventSignalUnderLock();
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
     * Pi 4). Ranges outside managed RAM are rejected by pmm_mark_range. */
    uint64_t rsv_addr, rsv_size;
    for (uint32_t i = 0; FdtGetReservedMem(i, &rsv_addr, &rsv_size); i++)
        PmmMarkRange((PhysAddr)rsv_addr, (PhysAddr)(rsv_addr + rsv_size));

    /* A bootloader-supplied initrd (DTB /chosen) lives outside the kernel
     * image, wherever it was loaded, so it needs its own reservation. */
    uint64_t initrd_start, initrd_end;
    if (FdtGetInitrd(&initrd_start, &initrd_end))
        PmmMarkRange((PhysAddr)initrd_start, (PhysAddr)initrd_end);
}

int PmmSubscribe(NtfnObj *ntfn)
{
    if (!ntfn)
        return ERR_BADARG;

    PmmSubscriber *new_node = KZAlloc(sizeof(PmmSubscriber));
    if (!new_node)
        return ERR_NOMEM;
    new_node->ntfn = ntfn;

    list_add_tail(&new_node->node, &pmm_subscribers.node);
    ntfn->ref_count++;

    return ZUZU_OK;
}

void PmmInit(void)
{
    // Compute PFN range from phys_region
    pmm_state.pfn_base = PhysToPfn(kernel_layout.ram_start);
    pmm_state.pfn_end = PhysToPfn(kernel_layout.ram_end);
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

    list_init(&pmm_subscribers);
}

PmmStats PmmGetStats(void)
{
    return (PmmStats){ .total_frames = pmm_state.total_frames, .free_frames = pmm_state.free_frames };
}

/* mark: mark pages in [start, end) as USED */
Err PmmMarkRange(PhysAddr start, PhysAddr end)
{
    if (start >= end)
        return ERR_BADARG;

    /* Align the range to page boundaries */
    PhysAddr astart = align_down(start, PAGE_SIZE);
    PhysAddr aend = align_up(end, PAGE_SIZE);

    Pfn start_pfn = PhysToPfn(astart);
    Pfn end_pfn = PhysToPfn(aend);

    /* PFN bounds check (pfn_end is exclusive) */
    if (start_pfn < pmm_state.pfn_base || end_pfn > pmm_state.pfn_end) {
        return ERR_BADARG;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.pfn_end > pmm_state.pfn_base);
    assert(pmm_state.total_frames == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_frames);

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
        }
    }

    return ZUZU_OK;
}

/* unmark: mark pages in [start, end) as FREE */
Err PmmUnmarkRange(const PhysAddr start, const PhysAddr end)
{
    if (start >= end)
        return ERR_BADARG;

    const PhysAddr astart = align_down(start, PAGE_SIZE);
    const PhysAddr aend = align_up(end, PAGE_SIZE);

    const Pfn start_pfn = PhysToPfn(astart);
    Pfn end_pfn = PhysToPfn(aend);

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
        }
    }

    return ZUZU_OK;
}

PhysAddr PmmAllocFrame(void)
{
    PhysAddr pa = PmmAllocFrameUnderLock();
    if (pa != 0) {
        SyspageUpdateMem();
    }
#ifdef PMM_TRACE
    KTRACE("alloc_page pa=%p pid=%u caller: %s", (void *)pa, current_pid_or_zero(),
           ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    return pa;
}
PhysAddr PmmAllocFramesContig(size_t n_frames)
{

    if (n_frames == 0 || pmm_state.free_frames < n_frames) {
        return PHYS_NULL;
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
                PhysAddr start_pa = PfnToPhys(pmm_state.pfn_base + start_index);
                PhysAddr end_pa = PfnToPhys(pmm_state.pfn_base + start_index + n_frames);
                if (PmmMarkRange(start_pa, end_pa) != ZUZU_OK) {
                    return PHYS_NULL; /* marking failed */
                }

                /* Keep freelist in sync without a full O(total_pages) rebuild. */
                PmmFreelistRemoveRange(start_pa, end_pa);

                Pfn pfn = pmm_state.pfn_base + start_index;
                PhysAddr addr = PfnToPhys(pfn);
                assert(addr % PAGE_SIZE == 0);
                assert(pfn >= pmm_state.pfn_base && (pfn + n_frames) <= pmm_state.pfn_end);
                SyspageUpdateMem(); // update free memory info in syspage
                PmmKEventSignalUnderLock();
#ifdef PMM_TRACE
                KTRACE("alloc_pages n=%zu pa=%p pid=%u scanned=%zu caller: %s", n_frames,
                       (void *)addr, current_pid_or_zero(), index + 1,
                       ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
                return addr;
            }
        } else {
            consecutive = 0; /* reset */
        }
    }

#ifdef PMM_TRACE
    KTRACE("alloc_pages n=%zu FAILED pid=%u scanned=%zu caller: %s", n_frames,
           current_pid_or_zero(), total_pages, ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    return PHYS_NULL;
}

void PmmFreeFrame(const PhysAddr addr)
{
#ifdef PMM_TRACE
    KTRACE("free_page pa=%p pid=%u caller: %s", (void *)addr, current_pid_or_zero(),
           ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    assert(addr % PAGE_SIZE == 0);

    const Pfn pfn = PhysToPfn(addr);

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
        PhysAddr *page_va = (PhysAddr *)PA_TO_VA(addr);
        *page_va = pmm_state.freelist_head;
        pmm_state.freelist_head = addr;

        SyspageUpdateMem();
        return;
    }

    /* already free -> double free: a live bug, not a bad argument */
    panic("pmm: double free of frame %x", pfn);
}

PhysAddr PmmAllocFramesContigAligned(const size_t n_frames, size_t align_frames)
{

    if (n_frames == 0) {
        return PHYS_NULL;
    }
    if (align_frames == 0)
        align_frames = 1;
#ifdef PMM_TRACE
    KTRACE("alloc_pages_aligned n=%zu align=%zu pid=%u caller: %s", n_frames, align_frames,
           current_pid_or_zero(), ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    // Require power-of-two alignment (common + cheap)
    if ((align_frames & (align_frames - 1)) != 0) {
        return PHYS_NULL;
    }

    if (pmm_state.free_frames < n_frames) {
        return PHYS_NULL;
    }

    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.total_frames == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_frames);

    const size_t total_pages = pmm_state.total_frames;
    size_t consecutive = 0;
    size_t start_index = 0;

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
                const uintptr_t start_pa = PfnToPhys(pmm_state.pfn_base + start_index);
                const uintptr_t end_pa = PfnToPhys(pmm_state.pfn_base + start_index + n_frames);

                if (PmmMarkRange(start_pa, end_pa) != ZUZU_OK) {
                    return PHYS_NULL;
                }

                /* Keep freelist in sync without a full O(total_pages) rebuild. */
                PmmFreelistRemoveRange(start_pa, end_pa);

                SyspageUpdateMem(); // update free memory info in syspage
                PmmKEventSignalUnderLock();
#ifdef PMM_TRACE
                KTRACE("alloc_pages_aligned n=%zu pa=%p pid=%u scanned=%zu caller: %s", n_frames,
                       (void *)start_pa, current_pid_or_zero(), index + 1,
                       ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif

                return start_pa;
            }
        } else {
            consecutive = 0;
        }
    }

#ifdef PMM_TRACE
    KTRACE("alloc_pages_aligned n=%zu FAILED pid=%u scanned=%zu caller: %s", n_frames,
           current_pid_or_zero(), total_pages, ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    return PHYS_NULL;
}

size_t PmmAllocFramesScattered(const size_t n_frames, PhysAddr *out_addrs)
{
#ifdef PMM_TRACE
    KTRACE("alloc_pages_scattered n=%zu pid=%u caller: %s", n_frames, current_pid_or_zero(),
           ksym_lookup((uint32_t)__builtin_return_address(0)));
#endif
    if (n_frames == 0 || !out_addrs || pmm_state.free_frames < n_frames) {
        return 0;
    }
    assert(pmm_state.bitmap != NULL);
    assert(pmm_state.total_frames == (size_t)(pmm_state.pfn_end - pmm_state.pfn_base));
    assert(pmm_state.bitmap_bytes * 8ULL >= pmm_state.total_frames);
    assert(pmm_state.free_frames <= pmm_state.total_frames);
    assert(n_frames <= pmm_state.total_frames);
    for (size_t i = 0; i < n_frames; i++) {
        const PhysAddr new_page = PmmAllocFrameUnderLock();
        if (new_page == 0) {
            SyspageUpdateMem();
            return i;
        }
        out_addrs[i] = new_page;
    }
    SyspageUpdateMem(); // update free memory info in syspage
    return n_frames;
}
