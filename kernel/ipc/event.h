/**
 * @file event.h
 * @brief Events are lightweight words that can be signalled and waited on, allowing asynchronous
 * work. Events are the foundation under multiple primitives, like kernel events, hardware
 * interrupts, or zones.
 */

#ifndef KERNEL_IPC_EVENT_H
#define KERNEL_IPC_EVENT_H

#include "observer_types.h"
#include <arch/regs.h>
#include <list.h>
#include <stdbool.h>
#include <types.h>

/**
 * @brief Forward declaration for SpaceObject.
 */
typedef struct SpaceObjectStruct SpaceObject;

/**
 * @brief The only kernel event bit, this is signalled when a threshold is reached in the available
 * frame count.
 */
#define EVENT_MEMMGMT_BIT (1u << 0)

/**
 * @brief Struct representing an event object.
 */
typedef struct EventObjectStruct {
    EventWord word;          /**< The word cleared on wake. */
    ListHead wait_queue;     /**< Tasks waiting on this event. */
    Spid owner_spid;         /**< Owner space's ID. */
    size_t ref_count;        /**< How many Spaces are holding this handle now? */
    bool alive;              /**< Is the Event destroyed? */
    EventWord bound_mask;    /**< Bitmask of bound bits. */
    uint16_t bound_refs[31]; /**< Each bit's bound reference counter. */
    ObserverSet observers;   /**< Parent events this event is bound into. */
    uint8_t child_count;     /**< Events bound into this one; nonzero means it is a parent. */
} EventObject;

/**
 * @brief Fill an event object with the given bits, optionally waking all waiters up.
 *
 * @param[in] ev The event to signal.
 * @param[in] bits The bits to carry onto the woken tasks.
 * @param[in] bcast If true, all waiters will be woken.
 *
 */
void EventSignal(EventObject *ev, EventWord bits, bool bcast);

/**
 * @brief Wait on an event to be signalled for @p timeout ms.
 *
 * @param[in] ev The event to wait on
 * @param[in] timeout The duration, in ms, to block for the event.
 * @param[in,out] frame The CPU frame to mutate when waking.
 * @param[in] mask The bit mask specifying the bits to wait on
 * @param[in] waitall Only wake waiter when all bits are signalled.
 */
void EventWait(EventObject *ev, Duration timeout, CpuState *frame, EventWord mask, bool waitall);

/**
 * @brief Reserve a bit of an event object, except the 31st, which is reserved for errors.
 *
 * @param[in] ev The event to bind on.
 * @param[in] bit The bit position to bind at.
 */
void EventBindBit(EventObject *ev, uint32_t bit);

/**
 * @brief Free up a bit of an event object.
 *
 * @param[in] ev The event to unbind from.
 * @param[in] bit The bit position to unbind.
 */
void EventUnbindBit(EventObject *ev, uint32_t bit);

/**
 * @brief Drop one reference from an event object, and destroy it if it is unreferenced.
 *
 * @param[in] ev The event to unreference.
 */
void EventUnref(EventObject *ev);

/**
 * @brief Wake all callers of @p ev with ERR_DEAD, and free the object.
 *
 * @param[in] ev The event to kill.
 */
void EventKill(EventObject *ev);

/**
 * @brief Create an event with @p owner as the owner space.
 *
 * @param[in] owner The owner space.
 *
 * @return EventObject* Pointer to a newly allocated event object.
 */
EventObject *EventCreate(SpaceObject *owner);

/**
 * @brief Wake all callers of @p ev with ERR_DEAD, and free the object.
 *
 * @param[in] ev The event to destroy.
 */
void EventDestroy(EventObject *ev);

#endif // KERNEL_IPC_EVENT_H
