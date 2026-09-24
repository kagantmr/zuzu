/**
 * @file space.h
 * @brief Space object definitions & methods.
 */

#ifndef _ZUZU_OBJECTS_SPACE_H
#define _ZUZU_OBJECTS_SPACE_H

#include "handle.h"
#include "kernel/mm/vmm.h"
#include "kernel/task/task.h"
#include <bitmap.h>
#include <zuzu/tls.h>

#define MAX_SPACES 512

/**
 * @brief Space object, representing a process space.
 */
typedef struct SpaceObjectStruct
{
    Spid spid, parent_spid;              /**< SPID of this space, and its parent space. */
    AddressSpace *as;                    /**< Pointer to the address space of this space. */
    ListNode node;                       /**< Embedded list node for space management. */
    ListNode destroy_node;               /**< Embedded list node for destruction management. */
    ListNode timeout_node;               /**< Embedded list node for timeout management. */
    Spid waiting_for;                    /**< SPID of the space this space is waiting for. */
    char name[32];                       /**< Space name. */
    ListHead outstanding_replies;        /**< List of outstanding replies. */
    HandleTable handle_table;            /**< Handle table for this space. */
    TaskObject *main_task;               /**< Pointer to the thread associated with this space. */
    ListHead tasks;                      /**< List of threads in this space. */
    ListHead kittens;                   /**< List of child spaces. */
    ListNode sibling_node;               /**< Embedded list node for sibling management. */
    PhysAddr tcb_page_pa[MAX_TCB_PAGES]; /**< TCB page physical addresses. */
    VirtAddr tcb_page_va;                /**< TCB page virtual address. */
    uint32_t tcb_slot_bitmap[BITMAP_WORDS(256)]; /**< TCB slot bitmap. */
    bool torn_down; /**< SpaceDestroy has already run; only a zombie main_task keeps
                         this struct allocated. See SpaceFinalize. */
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
static inline PhysAddr TcbSlotPhysAddr(SpaceObject *p, uint32_t slot)
{
    return p->tcb_page_pa[slot / SLOTS_PER_PAGE] + ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/* Kernel VA of this slot. */
static inline VirtAddr TcbSlotKVirtAddr(SpaceObject *p, uint32_t slot)
{
    return PA_TO_VA(p->tcb_page_pa[slot / SLOTS_PER_PAGE]) +
           ((slot % SLOTS_PER_PAGE) * TCB_SLOT_SIZE);
}

/* User VA of this slot. */
static inline VirtAddr TcbSlotUVirtAddr(SpaceObject *p, uint32_t slot)
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
SpaceObject *SpaceCreate(const char *name);

/**
 * @brief Find a space by its SPID.
 *
 * @param pid The SPID of the space to find.
 * @return The space object, or NULL if not found.
 */
SpaceObject *SpaceFindBySpid(Spid pid);
void SpaceReparent(SpaceObject *kitten, SpaceObject *parent);

SpaceObject *SpaceFindKittenBySpid(SpaceObject *parent, Spid pid);
SpaceObject *SpaceFindHollowKitten(SpaceObject *parent);

/**
 * @brief Find a child space whose representative task has already exited.
 *
 * Different predicate from SpaceFindHollowKitten: hollow means "no task was
 * ever launched," this means "the main task ran and reached ZOMBIE."
 */
SpaceObject *SpaceFindZombieKitten(SpaceObject *parent);

/**
 * @brief Tear down everything a space owns (ports, memory, events, handle
 * table, address space) and cascade-reparent its children to their
 * grandparent. Idempotent.
 *
 * Does not necessarily free the SpaceObject itself: if the space's last
 * task is still parked as a zombie (state == ZOMBIE, not yet reaped), the
 * struct is kept alive so that task's owner backpointer stays valid.
 * TaskDestroy calls SpaceFinalize() once that last task is actually freed.
 */
void SpaceDestroy(SpaceObject *sp);

/**
 * @brief Free a torn-down SpaceObject once no task references it anymore.
 * Only TaskDestroy should call this.
 */
void SpaceFinalize(SpaceObject *sp);

#endif /* _ZUZU_OBJECTS_SPACE_H */
