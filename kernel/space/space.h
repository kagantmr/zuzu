/**
 * @file space.h
 * @brief Space object definitions & methods.
 */


#ifndef _ZUZU_OBJECTS_SPACE_H
#define _ZUZU_OBJECTS_SPACE_H

#include "handle.h"
#include "kernel/mm/vmm.h"
#include "kernel/task/task.h"
#include <zuzu/tls.h>

/**
 * @brief Space object, representing a process space.
 */
typedef struct SpaceObjectStruct
{
    Spid pid, parent_pid;                /**< SPID of this space, and its parent space. */
    AddressSpace *as;                    /**< Pointer to the address space of this space. */
    ListNode node;                       /**< Embedded list node for space management. */
    ListNode destroy_node;               /**< Embedded list node for destruction management. */
    ListNode timeout_node;               /**< Embedded list node for timeout management. */
    Spid waiting_for;                    /**< SPID of the space this space is waiting for. */
    char name[32];                       /**< Space name. */
    ListHead outstanding_replies;        /**< List of outstanding replies. */
    HandleTable handle_table;            /**< Handle table for this space. */
    TaskObject *thread;                  /**< Pointer to the thread associated with this space. */
    ListHead threads;                    /**< List of threads in this space. */
    ListHead children;                   /**< List of child spaces. */
    ListNode sibling_node;               /**< Embedded list node for sibling management. */
    PhysAddr tcb_page_pa[MAX_TCB_PAGES]; /**< TCB page physical addresses. */
    VirtAddr tcb_page_va;                /**< TCB page virtual address. */
    uint32_t tcb_slot_bitmap[BITMAP_WORDS(256)]; /**< TCB slot bitmap. */
} SpaceObject;

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

#endif /* _ZUZU_OBJECTS_SPACE_H */
