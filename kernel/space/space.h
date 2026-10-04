/**
 * @file space.h
 * @brief Space object definitions & methods.
 */

#ifndef KERNEL_SPACE_SPACE_H
#define KERNEL_SPACE_SPACE_H

#include "handle.h"
#include "kernel/mm/vmm/vmm.h"
#include "kernel/task/task.h"
#include <bitmap.h>
#include <util/tls.h>

#define MAX_SPACES 512

/**
 * @brief Space object: an address space, its handle table and its tasks.
 */
typedef struct SpaceObjectStruct {
    Spid spid, parent_spid; /**< SPID of this space, and of the space that created it (-1: none). */
    AddressSpace *as;       /**< Pointer to the address space of this space. */
    char name[32];          /**< Space name. */
    HandleTable handle_table; /**< Handle table for this space. */
    TaskObject *main_task;    /**< Pointer to the first task associated with this space. */
    uint32_t max_prio;
    ListHead tasks;       /**< List of tasks in this space. */
    bool frozen;          /**< Space is frozen, nothing will execute. */
    uint32_t live_tasks;  /**< Count of live tasks */
    Err last_exit_status; /**< Anyone waiting on this Space will receive this upon hollowness. */
    ListHead waiters;
    PhysAddr tcb_page_pa[MAX_TCB_PAGES];         /**< TCB page physical addresses. */
    VirtAddr tcb_page_va;                        /**< TCB page virtual address. */
    uint32_t tcb_slot_bitmap[BITMAP_WORDS(256)]; /**< TCB slot bitmap. */
    bool torn_down; /**< SpaceDestroy has already run; only a zombie main_task keeps
                         this struct allocated. See SpaceFinalize. */
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

/* Physical base of the frame backing this slot's TCB page. */
static inline PhysAddr TcbSlotPa(SpaceObject *p, uint32_t slot)
{
    return p->tcb_page_pa[slot / SLOTS_PER_PAGE] + ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/* Kernel VA of this slot. */
static inline VirtAddr TcbSlotKernelVa(SpaceObject *p, uint32_t slot)
{
    return PA_TO_VA(p->tcb_page_pa[slot / SLOTS_PER_PAGE]) +
           ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/* User VA of this slot. */
static inline VirtAddr TcbSlotUserVa(SpaceObject *p, uint32_t slot)
{
    return p->tcb_page_va + ((slot / SLOTS_PER_PAGE) * PAGE_SIZE) +
           ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/**
 * @brief Create a new space.
 *
 * @param name The name of the space.
 * @return The space object, or NULL if not created.
 */
SpaceObject *SpaceCreate(const char *name, const SpaceObject *parent);

/**
 * @brief Find a space by its SPID.
 *
 * @param spid The SPID of the space to find.
 * @return The space object, or NULL if not found.
 */
SpaceObject *SpaceFindBySpid(Spid spid);

/**
 * @brief Tear down everything a space owns (ports, memory, events, handle
 * table, address space). Idempotent.
 *
 * Does not necessarily free the SpaceObject itself: if the space's last
 * task is still parked as a zombie (state == TASK_STATE_ZOMBIE, not yet reaped), the
 * struct is kept alive so that task's owner backpointer stays valid.
 * TaskDestroy calls SpaceFinalize() once that last task is actually freed.
 */
void SpaceDestroy(SpaceObject *sp);

void SpaceWaitHollow(SpaceObject *sp, Duration timeout, CpuState *frame);

/**
 * @brief Free a torn-down SpaceObject once no task references it anymore.
 * Only TaskDestroy should call this.
 */
void SpaceFinalize(SpaceObject *sp);

/**
 * @brief Undo TaskFault's freeze: clear frozen and re-queue any
 * sibling tasks that were left TASK_STATE_READY but unlinked from their run queue.
 */
void SpaceUnfreeze(SpaceObject *owner);

/* True if `target` is `sp` or one of the spaces that (transitively) created it. */
bool SpaceIsSelfOrAncestor(const SpaceObject *sp, const SpaceObject *target);

void SpaceRef(SpaceObject *sp);
void SpaceUnref(SpaceObject *sp);
/* No live tasks left: the condition observers wait for. */
bool SpaceIsHollow(const SpaceObject *sp);

#endif /* KERNEL_SPACE_SPACE_H */
