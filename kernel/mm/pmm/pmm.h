#ifndef KERNEL_MM_PMM_PMM_H
#define KERNEL_MM_PMM_PMM_H

#include "kernel/ipc/event.h"

#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <zuzu/err.h>

#define PAGE_SHIFT 12
#ifndef PAGE_SIZE
#define PAGE_SIZE (1u << PAGE_SHIFT)
#endif

typedef uint32_t Pfn;

static inline PhysAddr PfnToPa(Pfn pfn) { return (PhysAddr)pfn << PAGE_SHIFT; }
static inline Pfn PaToPfn(PhysAddr pa) { return (Pfn)(pa >> PAGE_SHIFT); }

#define PA_NULL ((PhysAddr)0)

typedef struct {
    size_t total_frames;
    size_t free_frames;
} PmmStats;

void PmmInit(void);

PmmStats PmmGetStats(void);

Err PmmMarkRange(PhysAddr start, PhysAddr end);

Err PmmUnmarkRange(PhysAddr start, PhysAddr end);

PhysAddr PmmAllocFrame(void);

PhysAddr PmmAllocFramesContig(size_t n_frames);

void PmmFreeFrame(PhysAddr addr);

PhysAddr PmmAllocFramesContigAligned(size_t n_frames, size_t align_frames);

size_t PmmAllocFramesScattered(size_t n_frames, PhysAddr *out_addrs);

int PmmSubscribe(EventObject *ev);

#endif
