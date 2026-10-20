/**
 * @file pmm.h
 * @brief Physical memory manager: frame allocation, range reservation, and pressure notification.
 */
#ifndef KERNEL_MM_PMM_PMM_H
#define KERNEL_MM_PMM_PMM_H

#include "kernel/ipc/event.h"

#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <zuzu/err.h>

/** @brief log2 of the page size. */
#define PAGE_SHIFT 12
#ifndef PAGE_SIZE
/** @brief Size of a page / physical frame in bytes. */
#define PAGE_SIZE (1u << PAGE_SHIFT)
#endif

/** @brief Physical frame number (physical address divided by PAGE_SIZE). */
typedef uint32_t Pfn;

/**
 * @brief Convert a frame number to its physical address.
 * @param[in] pfn Frame number.
 * @return Physical address of the start of the frame.
 */
static inline PhysAddr PfnToPa(Pfn pfn) { return (PhysAddr)pfn << PAGE_SHIFT; }

/**
 * @brief Convert a physical address to its frame number.
 * @param[in] pa Physical address.
 * @return Frame number containing @p pa.
 */
static inline Pfn PaToPfn(PhysAddr pa) { return (Pfn)(pa >> PAGE_SHIFT); }

/** @brief Physical address value returned by allocators on failure. */
#define PA_NULL ((PhysAddr)0)

/** @brief Snapshot of physical memory usage. */
typedef struct {
    size_t total_frames; /**< Total number of frames managed. */
    size_t free_frames;  /**< Number of frames currently free. */
} PmmStats;

/**
 * @brief Initialise the PMM from the boot RAM layout.
 *
 * Places the allocation bitmap after the kernel image, reserves boot, kernel,
 * stack, DTB, reserved-memory and initrd regions, builds the free list, and
 * initialises the subscriber list.
 */
void PmmInit(void);

/**
 * @brief Read the current frame counts.
 * @return Total and free frame counts.
 */
PmmStats PmmGetStats(void);

/**
 * @brief Mark a physical range as allocated.
 *
 * The range is widened to page boundaries. Frames already marked are left as is.
 *
 * @param[in] start Start of the range.
 * @param[in] end End of the range (exclusive).
 * @return ZUZU_OK on success, or an error code.
 * @retval ERR_BADARG Empty range, or range outside managed RAM.
 */
Err PmmMarkRange(PhysAddr start, PhysAddr end);

/**
 * @brief Mark a physical range as free.
 *
 * The range is widened to page boundaries. Frames already free are left as is.
 *
 * @param[in] start Start of the range.
 * @param[in] end End of the range (exclusive).
 * @return ZUZU_OK on success, or an error code.
 * @retval ERR_BADARG Empty range, or range outside managed RAM.
 */
Err PmmUnmarkRange(PhysAddr start, PhysAddr end);

/**
 * @brief Allocate one frame.
 * @return Physical address of the frame, or PA_NULL if none is free.
 */
PhysAddr PmmAllocFrame(void);

/**
 * @brief Allocate a run of physically contiguous frames (first fit).
 * @param[in] n_frames Number of frames, non-zero.
 * @return Physical address of the first frame, or PA_NULL on failure.
 */
PhysAddr PmmAllocFramesContig(size_t n_frames);

/**
 * @brief Free one frame.
 *
 * Panics if the frame is not currently allocated (double free).
 *
 * @param[in] addr Page-aligned physical address of the frame.
 */
void PmmFreeFrame(PhysAddr addr);

/**
 * @brief Allocate contiguous frames whose start frame number is aligned.
 * @param[in] n_frames Number of frames, non-zero.
 * @param[in] align_frames Alignment in frames; must be a power of two (0 is treated as 1).
 * @return Physical address of the first frame, or PA_NULL on failure or invalid alignment.
 */
PhysAddr PmmAllocFramesContigAligned(size_t n_frames, size_t align_frames);

/**
 * @brief Allocate frames that need not be contiguous.
 *
 * May return fewer frames than requested if the free list runs out part way;
 * frames already taken are not released.
 *
 * @param[in] n_frames Number of frames wanted.
 * @param[out] out_addrs Array of at least @p n_frames entries receiving the frame addresses.
 * @return Number of frames actually allocated (0 for invalid arguments or insufficient free memory).
 */
size_t PmmAllocFramesScattered(size_t n_frames, PhysAddr *out_addrs);

/**
 * @brief Subscribe an event object to low-memory notifications.
 *
 * The PMM takes a reference on the event and signals EVENT_MEMMGMT_BIT when free
 * memory falls below the low-water mark. Subscribers whose event is no longer
 * alive are dropped on the next signal.
 *
 * @param[in,out] ev Event object to signal.
 * @return ZUZU_OK on success, or an error code.
 * @retval ERR_BADARG @p ev is NULL.
 * @retval ERR_NOMEM Subscriber allocation failed.
 */
int PmmSubscribe(EventObject *ev);

#endif
