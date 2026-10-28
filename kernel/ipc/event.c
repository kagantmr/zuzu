/**
 * @file event.c
 * @brief Event object function implementations.
 */

#include "event.h"

#include "core/ensure.h"
#include "core/panic.h"
#include "kernel/mm/alloc.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"
#include <string.h>

#include <assert.h>
#include <types.h>
#include <zuzu/err.h>

/**
 * @brief Heap cache for event object slabs.
 */
static KSlabCache event_cache;

/**
 * @brief Allocate a new event object from the slab.
 *
 * @return EventObject* A freshly allocated event object.
 */
static EventObject *EventObjAlloc(void)
{
    if (!event_cache.obj_size)
        KSlabInit(&event_cache, sizeof(EventObject));
    return KSlabAlloc(&event_cache);
}

/**
 * @brief Checks if the bitmask matches the signalled bits.
 * 
 * @param[in] word The word to check
 * @param[in] mask The mask to use
 * @param[in] all If all of the bits must be checked
 * 
 * @return EventWord
 */
static EventWord EventTake(EventWord word, EventWord mask, bool all)
{
    EventWord hit = word & mask;
    if (all)
        return (hit == mask) ? mask : 0;
    return hit;
}

/**
 * @brief Free an existing event object.
 *
 * @param[in] ev The object to free.
 */
static void EventObjFree(EventObject *ev) { KSlabFree(&event_cache, ev); }

void EventSignal(EventObject *ev, EventWord bits, bool bcast)
{
    assert(ev && ev->alive && !(bits & EVENT_WAIT_ALL));
    ev->word |= bits;

    EventWord delivered = 0;
    ListNode *n = ev->wait_queue.node.next;
    while (n != &ev->wait_queue.node) {
        ListNode *next = n->next;
        TaskObject *w = container_of(n, WaitSlot, node)->owner;
        EventWord got = EventTake(ev->word, w->wait_mask, w->wait_all);
        if (got) {
            ListRemove(n);
            ArchSetInFrame(w->trap_frame, 0, ZUZU_OK);
            (*ArchGetFromFrame(w->trap_frame, 1)) = (Register)got;
            SchedUnblock(w);
            SchedAdd(w);
            delivered |= got;
            if (!bcast)
                break;
        }
        n = next;
    }
    ev->word &= ~delivered;
}

void EventKill(EventObject *ev)
{
    if (!ev || !ev->alive)
        return;
    ev->alive = false;
    while (!ListIsEmpty(&ev->wait_queue)) {
        ListNode *n = ListPopFront(&ev->wait_queue);
        WaitSlot *slot = container_of(n, WaitSlot, node);
        TaskAbortWait(slot->owner, ERR_DEAD);
    }
}

void EventUnref(EventObject *ev)
{
    if (!ev)
        return;
    if (ev->ref_count > 0)
        ev->ref_count--;
    if (ev->ref_count > 0)
        return;
    EventKill(ev);
    EventObjFree(ev);
}

void EventBindBit(EventObject *ev, uint32_t bit)
{
    if (bit >= 31)
        return;
    if (ev->bound_refs[bit] < UINT16_MAX)
        ev->bound_refs[bit]++;
    ev->bound_mask |= (1U << bit);
}

void EventUnbindBit(EventObject *ev, uint32_t bit)
{
    if (bit >= 31 || ev->bound_refs[bit] == 0)
        return;
    if (--ev->bound_refs[bit] == 0)
        ev->bound_mask &= ~(1U << bit);
}

EventObject *EventCreate(SpaceObject *owner)
{
    EventObject *ev = EventObjAlloc();
    ENSURE_RET(ev, NULL);

    ev->owner_spid = owner->spid;
    ev->ref_count = 1;
    ev->alive = true;
    ListInit(&ev->wait_queue);
    ev->word = 0;
    ev->bound_mask = 0;
    memset(ev->bound_refs, 0, sizeof(ev->bound_refs));

    return ev;
}

void EventWait(EventObject *ev, Duration timeout, CpuState *frame, EventWord mask, bool waitall)
{
    ENSURE_ERR(frame, ev->alive, ERR_DEAD);
    ENSURE_ERR(frame, !(waitall && mask == 0), ERR_BADARG);
    if (mask == 0)
        mask = ~EVENT_WAIT_ALL;          /* 0 means every bit */

    EventWord got = EventTake(ev->word, mask, waitall);
    if (got) {
        ev->word &= ~got;
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, (Register)got);
        return;
    }
    current_task->wait_mask = mask;
    current_task->wait_all = waitall;
    SchedBlockOn(&ev->wait_queue, timeout);
}
