#ifndef KERNEL_MM_PMM_PMM_INTERNAL_H
#define KERNEL_MM_PMM_PMM_INTERNAL_H

#include "pmm.h"
#include <list.h>
#include <stdbool.h>

typedef struct {
    Pfn pfn_base;
    Pfn pfn_end;
    size_t total_frames;
    size_t free_frames;
    uint8_t *bitmap;
    size_t bitmap_bytes;
    PhysAddr freelist_head;
    bool freelist_ready;
    bool in_pressure;
} PmmState;

extern PmmState pmm_state;
extern ListHead pmm_subscribers;

void PmmSignalSubscribers(void);

#endif /* KERNEL_MM_PMM_PMM_INTERNAL_H */
