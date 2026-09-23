#ifndef NOTIF_H
#define NOTIF_H

#include <list.h>
#include <stdbool.h>
#include <zuzu/types.h>

typedef struct SpaceObjectStruct SpaceObject;

typedef struct EventObjectStruct
{
    EventWord word;      // 31-bit signal mask (bit 31 reserved), atomic-ish (IRQs off)
    ListHead wait_queue; // processes blocked in WaitOn()
    SpaceObject *owner;
    Spid owner_spid;
    size_t ref_count;
    bool alive;
} EventObject;

struct wait_slot;

/**
 * @brief Wake one waiter already popped from ev->wait_queue.
 * @param ev     Notification the waiter was popped from.
 * @param slot     Waiter's slot, already popped.
 * @param r0_value Lands in the waiter's r0: delivered bits from EventSignal(),
 *                 or a negative error from cap_destroy.
 * @note A queued waiter without a trap frame is a corrupt wait queue:
 * panics rather than limp past it.
 */
void EventWakeWaiter(EventObject *ev, struct wait_slot *slot, int32_t r0_value);

/**
 * @brief Signal one or more bits on an event object.
 *
 * ORs @p bits into the event's word and wakes at most one waiter.
 * If a waiter is woken, it receives the accumulated word and the word is
 * cleared. If no waiter is queued, bits stay pending for the next wait.
 *
 * @param ev  Live event object. Must not be NULL.
 * @param bits  Bits to signal. Bit 31 is reserved and must be zero.
 * @pre         Caller has verified @p ev is alive and @p bits is valid.
 * @pre         IRQs disabled.
 */
void EventSignal(EventObject *ev, EventWord bits);

void EventDropReference(EventObject *ev);

/* Slab-backed evObj pool. KAllocev returns uninitialized storage. */
EventObject *EventObjAlloc(void);
void EventObjFree(EventObject *ev);

EventObject *EventCreate(SpaceObject *owner);
void EventDestroy(EventObject *ev);

#endif // NOTIF_H
