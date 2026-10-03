#include "event.h"

#include "core/ensure.h"
#include "core/panic.h"
#include "kernel/mm/alloc.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"

#include <assert.h>
#include <types.h>
#include <zuzu/err.h>

static KHeapSlabCache event_cache;

static EventObject *EventObjAlloc(void)
{
    if (!event_cache.obj_size)
        KSlabInit(&event_cache, sizeof(EventObject));
    return KSlabAlloc(&event_cache);
}

static void EventObjFree(EventObject *ev) { KSlabFree(&event_cache, ev); }


void EventSignal(EventObject *ev, EventWord bits, bool bcast)
{
    assert(ev && ev->alive && !(bits & (1U << 31)));
    ev->word |= bits;

    bool delivered = false;
    while (!list_empty(&ev->wait_queue))
    {
        ListNode *node = list_pop_front(&ev->wait_queue);
        WaitSlot *slot = container_of(node, WaitSlot, node);
        TaskObject *waiter = slot->owner;
        assert(waiter && waiter->trap_frame);
        ArchSetInFrame(waiter->trap_frame, 0, ZUZU_OK);
        (*ArchGetFromFrame(waiter->trap_frame, 1)) = (Register)ev->word;
        SchedUnblock(waiter);
        SchedAdd(waiter);
        delivered = true;
        if (!bcast)
            break;
    }

    if (delivered)
        ev->word = 0;
}

void EventKill(EventObject *ev)
{
    if (!ev || !ev->alive)
        return;
    ev->alive = false;
    while (!list_empty(&ev->wait_queue))
    {
        ListNode *n = list_pop_front(&ev->wait_queue);
        WaitSlot *slot = container_of(n, WaitSlot, node);
        TaskAbortWait(slot->owner, ERR_DEAD);
    }
}

void EventDropReference(EventObject *ev)
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

EventObject *EventCreate(SpaceObject *owner)
{
    EventObject *ev = EventObjAlloc();
    ENSURE_RET(ev, NULL);

    ev->owner_spid = owner->spid;
    ev->ref_count = 1;
    ev->alive = true;
    list_init(&ev->wait_queue);
    ev->word = 0;
    ev->bound_mask = 0;
    
    return ev;
}

void EventDestroy(EventObject *ev)
{
    if (!ev || !ev->alive)
        return;
    EventKill(ev);
    EventDropReference(ev);
}

void EventWait(EventObject *ev,Duration timeout, CpuState *frame) {
    ENSURE_ERR(frame, ev->alive, ERR_DEAD);
    if (ev->word) {
        ArchSetInFrame(frame, 0, ZUZU_OK);
        ArchSetInFrame(frame, 1, (Register)ev->word);
        ev->word = 0;
        return;
    }

    SchedBlockOn(&ev->wait_queue, timeout);
}

