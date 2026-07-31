#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include <zuzu/types.h>

#define PAGE_SHIFT 12
#ifndef PAGE_SIZE
#define PAGE_SIZE (1u << PAGE_SHIFT) // A page is 4KB
#endif

typedef uint32_t Pfn;

typedef struct
{
    Pfn pfn_base;      // lowest page frame number
    Pfn pfn_end;       // highest page frame number (exclusive)
    size_t total_frames;     // total number of pages
    size_t free_frames;      // updated at runtime
    uint8_t *bitmap;        // pointer to bitmap memory
    size_t bitmap_bytes;    // size of bitmap in bytes
    PhysAddr freelist_head; // PA of first free page (or 0 if none)
} PmmState;

static inline PhysAddr PfnToPhys(Pfn pfn) { return (PhysAddr)pfn << PAGE_SHIFT; }
static inline Pfn PhysToPfn(PhysAddr pa)  { return (Pfn)(pa >> PAGE_SHIFT); }

#define PHYS_NULL ((PhysAddr)0)

/**
 * @brief Initialize the physical memory manager.
 * This sets up the bitmap and marks reserved regions.
 */
void PmmInit(void);

/**
 * @brief Mark a range of physical frames as used.
 * @return ZUZU_OK if successful, ERR_BADARG if the range is invalid.
 */
Err PmmMarkRange(const PhysAddr start, const PhysAddr end);

/**
 * @brief Unmark a range of frames.
 * @return ZUZU_OK if successful, ERR_BADARG if the range is invalid.
 */
Err PmmUnmarkRange(const PhysAddr start, const PhysAddr end);

/**
 * @brief Allocates a physical frame, and returns a pointer to it.
 * @return Address of the allocated frame.
 */
PhysAddr PmmAllocFrame(void);

/**
 * @brief Allocates contiguous physical frames.
 *
 * @param n_frames Number of frames to allocate.
 *
 * @return Address of the first allocated frame.
 */
PhysAddr PmmAllocFramesContig(const size_t n_frames);

/**
 * @brief Marks an allocated frame as unallocated.
 *
 * @param addr Address of the allocated frame.
 *
 * @return ZUZU_OK if successful, ERR_BADARG if addr is invalid. Panics on double free.
 */
void PmmFreeFrame(const PhysAddr addr);

/**
 * @brief Allocates contiguous physical frames with specific alignment.
 *
 * @param n_frames Number of pages to allocate.
 * @param align_frames Alignment in pages (must be power of two).
 *
 * @return Address of the first allocated page.
 */
PhysAddr PmmAllocFramesContigAligned(const size_t n_frames, const size_t align_frames);

/**
 * @brief Allocates scattered physical frames and returns their addresses.
 *
 * @param n_frames Number of frame to allocate.
 * @param out_addrs Caller-provided array for frame addresses
 *
 * @return Amount of frame found (may not match n_frames if zuzu is OOM)
 */
size_t PmmAllocFramesScattered(const size_t n_frames, PhysAddr *out_addrs);

#endif