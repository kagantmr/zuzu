/**
 * @file pmm_internal.h
 * @brief Private PMM state shared between pmm.c and pmm_pressure.c.
 */
#ifndef KERNEL_MM_PMM_PMM_INTERNAL_H
#define KERNEL_MM_PMM_PMM_INTERNAL_H

#include "pmm.h"
#include <list.h>
#include <stdbool.h>

/** @brief Global state of the physical memory manager. */
typedef struct {
    Pfn pfn_base;           /**< First managed frame number. */
    Pfn pfn_end;            /**< One past the last managed frame number. */
    size_t total_frames;    /**< Number of managed frames. */
    size_t free_frames;     /**< Number of free frames. */
    uint8_t *bitmap;        /**< Allocation bitmap, one bit per frame (1 = allocated). */
    size_t bitmap_bytes;    /**< Bytes of bitmap actually in use. */
    PhysAddr freelist_head; /**< Physical address of the first free frame, 0 if the list is empty. */
    bool freelist_ready;    /**< True once the free list has been built. */
    bool in_pressure;       /**< True while memory is below the low-water mark. */
} PmmState;

/** @brief The single PMM state instance. */
extern PmmState pmm_state;
/** @brief List of low-memory subscribers. */
extern ListHead pmm_subscribers;

/**
 * @brief Check free-memory levels and signal subscribers on entering memory pressure.
 *
 * Signals once when free memory drops below the low-water mark; pressure is
 * cleared once it rises above the high-water mark.
 */
void PmmSignalSubscribers(void);

#endif /* KERNEL_MM_PMM_PMM_INTERNAL_H */
