#ifndef KERNEL_SPACE_SPACE_H
#define KERNEL_SPACE_SPACE_H

#include "handle.h"
#include "kernel/mm/vmm/vmm.h"
#include "kernel/task/task.h"
#include <bitmap.h>
#include <util/tls.h>

#define MAX_SPACES 512

typedef struct SpaceObjectStruct {
    Spid spid, parent_spid;
    AddressSpace *as;
    char name[32];
    HandleTable handle_table;
    TaskObject *main_task;
    uint32_t max_prio;
    ListHead tasks;
    bool frozen;
    uint32_t live_tasks;
    Err last_exit_status;
    ListHead waiters;
    PhysAddr tcb_page_pa[MAX_TCB_PAGES];
    VirtAddr tcb_page_va;
    uint32_t tcb_slot_bitmap[BITMAP_WORDS(256)];
    bool torn_down; 
    ObserverSet observers;
    uint32_t ref_count;
} SpaceObject;

_Static_assert(TCB_MAX_SLOTS <= 256, "tcb_slot_bitmap is 256 bits wide");

static inline int TcbSlotAlloc(SpaceObject *p)
{
    int slot = BitmapFindFirstZero(p->tcb_slot_bitmap, TCB_MAX_SLOTS);
    if (slot < 0)
        return -1;
    BitmapSet(p->tcb_slot_bitmap, (size_t)slot);
    return slot;
}

static inline void TcbSlotFree(SpaceObject *p, int slot)
{
    BitmapClr(p->tcb_slot_bitmap, (size_t)slot);
}

static inline PhysAddr TcbSlotPa(SpaceObject *p, uint32_t slot)
{
    return p->tcb_page_pa[slot / SLOTS_PER_PAGE] + ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

static inline VirtAddr TcbSlotKernelVa(SpaceObject *p, uint32_t slot)
{
    return PA_TO_VA(p->tcb_page_pa[slot / SLOTS_PER_PAGE]) +
           ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

static inline VirtAddr TcbSlotUserVa(SpaceObject *p, uint32_t slot)
{
    return p->tcb_page_va + ((slot / SLOTS_PER_PAGE) * PAGE_SIZE) +
           ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

SpaceObject *SpaceCreate(const char *name, const SpaceObject *parent);

SpaceObject *SpaceFindBySpid(Spid spid);

void SpaceDestroy(SpaceObject *sp);

void SpaceWaitHollow(SpaceObject *sp, Duration timeout, CpuState *frame);

void SpaceFinalize(SpaceObject *sp);

void SpaceUnfreeze(SpaceObject *owner);

bool SpaceIsSelfOrAncestor(const SpaceObject *sp, const SpaceObject *target);

void SpaceRef(SpaceObject *sp);
void SpaceUnref(SpaceObject *sp);
bool SpaceIsHollow(const SpaceObject *sp);

#endif /* KERNEL_SPACE_SPACE_H */
