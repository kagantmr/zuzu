#include "event.h"

#include "core/ensure.h"
#include "core/panic.h"
#include "kernel/mm/alloc.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"

#include <assert.h>
#include <zuzu/types.h>
#include <zuzu/err.h>

static KHeapSlabCache event_cache;

EventObject *EventObjAlloc(void)
{
    if (!event_cache.obj_size)
        KSlabInit(&event_cache, "NtfnObj", sizeof(EventObject));
    return KSlabAlloc(&event_cache);
}

void EventObjFree(EventObject *ev) { KSlabFree(&event_cache, ev); }

void EventWakeWaiter(EventObject *ev, WaitSlot *slot, EventWord bits)
{
    TaskObject *waiter = slot->owner;
    if (!waiter || !waiter->trap_frame)
    {
        panic("NtfnWakeWaiter: queued waiter with no trap frame "
              "(ntfn=%p slot=%p owner=%p trap_frame=%p)",
              (void *)ev, (void *)slot, (void *)waiter,
              waiter ? (void *)waiter->trap_frame : NULL);
    }

    ArchSetInFrame(waiter->trap_frame, 0, ZUZU_OK);
    (*ArchGetFromFrame(waiter->trap_frame, 1)) = (Register)bits;
    SchedUnblock(waiter, WAKE_IPC);
    SchedAdd(waiter);
}

void EventSignal(EventObject *ev, EventWord bits)
{
    assert(ev && ev->alive && !(bits & (1U << 31)));
    ev->word |= bits;
    if (!list_empty(&ev->wait_queue))
    {
        ListNode *node = list_pop_front(&ev->wait_queue);
        WaitSlot *slot = container_of(node, WaitSlot, node);
        EventWord delivered = ev->word;
        EventWakeWaiter(ev, slot, delivered);
        ev->word = 0;
    }
}

void EventDropReference(EventObject *ev)
{
    if (!ev)
        return;
    ev->ref_count--;
    if (ev->ref_count == 0)
    {
        EventObjFree(ev);
    }
}

EventObject *EventCreate(SpaceObject *owner)
{
    EventObject *ev = EventObjAlloc();
    ENSURE_RET(ev, NULL);

    ev->owner_spid = owner->spid;
    ev->owner = owner;
    ev->ref_count = 1;
    ev->alive = true;
    list_init(&ev->wait_queue);
    ev->word = 0;
    
    return ev;
}

void EventDestroy(EventObject *ev)
{
    if (!ev || !ev->alive)
        return;

    while (!list_empty(&ev->wait_queue))
    {
        ListNode *n = list_pop_front(&ev->wait_queue);
        WaitSlot *slot = container_of(n, WaitSlot, node);
        EventWakeWaiter(ev, slot, (EventWord)ERR_DEAD);
    }
    ev->alive = false;

    EventDropReference(ev);
}