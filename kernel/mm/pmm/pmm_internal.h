#ifndef ZUZU_PMM_INTERNAL_H
#define ZUZU_PMM_INTERNAL_H

/* Cross-TU glue shared by pmm.c and pmm_pressure.c. Not part of the public
 * pmm.h API -- do not include from outside kernel/mm/pmm/. */

#include "pmm.h"
#include <list.h>
#include <stdbool.h>

typedef struct {
    Pfn pfn_base;           // lowest page frame number
    Pfn pfn_end;            // highest page frame number (exclusive)
    size_t total_frames;    // total number of pages
    size_t free_frames;     // updated at runtime
    uint8_t *bitmap;        // pointer to bitmap memory
    size_t bitmap_bytes;    // size of bitmap in bytes
    PhysAddr freelist_head; // PA of first free page (or 0 if none)
    bool in_pressure;       // notify if memory is going low to signal via Event
} PmmState;

extern PmmState pmm_state;
extern ListHead pmm_subscribers;

/* Signal low/recovered memory pressure to subscribers; called after every
 * allocation that can move free_frames across a watermark. */
void PmmKEventSignal(void);

#endif /* ZUZU_PMM_INTERNAL_H */
