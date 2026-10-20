/**
 * @file space.h
 * @brief A space is a perspective of memory and objects that tasks may operate under. Handle tables are per-space,
 * and objects are shared by a space's tasks. 
 */

#ifndef KERNEL_SPACE_SPACE_H
#define KERNEL_SPACE_SPACE_H

#include "handle.h"
#include "kernel/mm/vmm/vmm.h"
#include "kernel/task/task.h"
#include <bitmap.h>
#include <util/tls.h>

/**
 * @brief Kernel-wide limit on the maximum number of spaces.
 */
#define MAX_SPACES 512

/**
 * @brief Represents a space object.
 */
typedef struct SpaceObjectStruct {
    Spid spid; /**< The space's ID, unique among live spaces. */
    Spid parent_spid; /**< The ID of the space that created this one, or -1 if none. */
    AddressSpace *as; /**< The address space, or NULL once the space is torn down. */
    char name[32]; /**< The space's name, truncated to fit. */
    HandleTable handle_table; /**< The handles the space's tasks share. */
    TaskObject *main_task; /**< The first task created in the space, or NULL once it has been destroyed. */
    uint32_t max_prio; /**< The highest priority a task in this space may set. */
    ListHead tasks; /**< All tasks in the space, live or not yet reaped. */
    bool frozen; /**< A task faulted; none of the space's tasks are scheduled until unfrozen. */
    uint32_t live_tasks; /**< The number of tasks that have not exited. */
    Err last_exit_status; /**< The exit status of the last task to exit. */
    ListHead waiters; /**< Tasks waiting for the space to become hollow. */
    PhysAddr tcb_page_pa[MAX_TCB_PAGES]; /**< Physical pages of the TLS window; 0 where not allocated. */
    VirtAddr tcb_page_va; /**< Where the TLS window is mapped in the space. */
    uint32_t tcb_slot_bitmap[BITMAP_WORDS(256)]; /**< One bit per TLS slot, set while in use. */
    bool torn_down; /**< SpaceDestroy() has run; only a zombie main task may keep the object alive. */
    ObserverSet observers; /**< Events signalled when the space becomes hollow. */
    uint32_t ref_count; /**< References held by handles, tasks and teardown in progress. */
} SpaceObject;

_Static_assert(TCB_MAX_SLOTS <= 256, "tcb_slot_bitmap is 256 bits wide");

/**
 * @brief Allocate a slot for the TLS page in a space.
 * 
 * @param[in] sp The space to allocate it in.
 * 
 * @return int
 */
static inline int TcbSlotAlloc(SpaceObject *sp)
{
    int slot = BitmapFindFirstZero(sp->tcb_slot_bitmap, TCB_MAX_SLOTS);
    if (slot < 0)
        return -1;
    BitmapSet(sp->tcb_slot_bitmap, (size_t)slot);
    return slot;
}

/**
 * @brief Free the slot of a TLS page in a space.
 * 
 * @param[in] sp The space to free it from.
 * @param[in] slot The TCB slot to free.
 * 
 */
static inline void TcbSlotFree(SpaceObject *sp, int slot)
{
    BitmapClr(sp->tcb_slot_bitmap, (size_t)slot);
}

/**
 * @brief Get the physical address of a TLS page in a space.
 * 
 * @param[in] sp The space to look up in.
 * @param[in] slot The TCB slot.
 * 
 * @return PhysAddr
 */
static inline PhysAddr TcbSlotPa(SpaceObject *sp, uint32_t slot)
{
    return sp->tcb_page_pa[slot / SLOTS_PER_PAGE] + ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/**
 * @brief Get the kernel-visible virtual address of a TLS page in a space.
 * 
 * @param[in] sp The space to look up in.
 * @param[in] slot The TCB slot.
 * 
 * @return VirtAddr
 */
static inline VirtAddr TcbSlotKernelVa(SpaceObject *sp, uint32_t slot)
{
    return PA_TO_VA(sp->tcb_page_pa[slot / SLOTS_PER_PAGE]) +
           ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/**
 * @brief Get the user-visible virtual address of a TLS page in a space.
 * 
 * @param[in] sp The space to look up in.
 * @param[in] slot The TCB slot.
 * 
 * @return VirtAddr
 */
static inline VirtAddr TcbSlotUserVa(SpaceObject *sp, uint32_t slot)
{
    return sp->tcb_page_va + ((slot / SLOTS_PER_PAGE) * PAGE_SIZE) +
           ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/**
 * @brief Create a new space object with a given parent.
 * 
 * @param[in] name The name of the space.
 * @param[in] parent The parent of this kitten space.
 * 
 * @return SpaceObject*
 */
SpaceObject *SpaceCreate(const char *name, const SpaceObject *parent);

/**
 * @brief Find a space object from its space ID.
 * 
 * @param[in] spid The space ID of the space.
 * 
 * @return SpaceObject*
 */
SpaceObject *SpaceFindBySpid(Spid spid);

/**
 * @brief Tear down a space: terminate its tasks, wake anyone waiting on it, release its
 * interrupts and handles, and free its address space.
 *
 * The object itself is freed once nothing references it any more. Calling this twice is harmless.
 *
 * @param[in] sp The space to tear down.
 */
void SpaceDestroy(SpaceObject *sp);

/**
 * @brief Wait for a space to become hollow, meaning it has no live tasks.
 *
 * Replies at once if it already is, with its last exit status in r1; otherwise blocks the
 * calling task. Fails with ERR_BADARG for the caller's own space and ERR_DEAD for one that has
 * been torn down.
 *
 * @param[in] sp The space to wait on.
 * @param[in] timeout Milliseconds to wait, or TIMEOUT_POLL / TIMEOUT_INFINITE.
 * @param[in] frame The caller's trap frame, which receives the result.
 */
void SpaceWaitHollow(SpaceObject *sp, Duration timeout, CpuState *frame);

/**
 * @brief Free a space if it is torn down, has no tasks left, and nothing references it.
 *
 * Does nothing otherwise, so it is safe to call at any point after a reference is dropped.
 *
 * @param[in] sp The space to free.
 */
void SpaceFinalize(SpaceObject *sp);

/**
 * @brief Clear a space's frozen flag and put its ready tasks back on the run queue.
 *
 * @param[in] owner The space to unfreeze.
 */
void SpaceUnfreeze(SpaceObject *owner);

/**
 * @brief Check whether one space is another or descends from it.
 *
 * @param[in] sp The space to start from.
 * @param[in] target The space to look for among @p sp and its ancestors.
 * @return true if @p target is @p sp or one of its ancestors.
 */
bool SpaceIsSelfOrAncestor(const SpaceObject *sp, const SpaceObject *target);

/**
 * @brief Take a reference on a space.
 *
 * @param[in] sp The space.
 */
void SpaceRef(SpaceObject *sp);

/**
 * @brief Drop a reference on a space, freeing it if that was the last one and it is torn down.
 *
 * @param[in] sp The space.
 */
void SpaceUnref(SpaceObject *sp);

/**
 * @brief Check whether a space has no live tasks.
 *
 * @param[in] sp The space.
 * @return true if every task in it has exited.
 */
bool SpaceIsHollow(const SpaceObject *sp);

#endif /* KERNEL_SPACE_SPACE_H */
