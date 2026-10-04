#include "port.h"
#include "core/ensure.h"
#include "kernel/ipc/msg.h"
#include "kernel/mm/alloc.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"
#include <zuzu/err.h>

static KSlabCache port_cache;

static PortObject *PortObjAlloc(void)
{
    if (!port_cache.obj_size)
        KSlabInit(&port_cache, sizeof(PortObject));
    return KSlabAlloc(&port_cache);
}

static void PortObjFree(PortObject *port) { KSlabFree(&port_cache, port); }

PortObject *PortCreate(SpaceObject *owner)
{
    PortObject *new_port = PortObjAlloc();
    ENSURE_RET((NULL != new_port), NULL);

    list_init(&new_port->sender_queue);
    list_init(&new_port->receiver_queue);
    new_port->owner_spid = owner->spid;
    new_port->ref_count = 1;
    new_port->alive = true;
    ObserverInit(&new_port->observers);

    return new_port;
}

void PortKill(PortObject *port)
{
    if (!port || !port->alive)
        return;
    port->alive = false;

    while (!list_empty(&port->sender_queue)) {
        ListNode *n = list_pop_front(&port->sender_queue);
        TaskObject *t = container_of(n, TaskObject, node);
        TaskAbortWait(t, ERR_DEAD);
    }

    while (!list_empty(&port->receiver_queue)) {
        ListNode *n = list_pop_front(&port->receiver_queue);
        WaitSlot *slot = container_of(n, WaitSlot, node);
        TaskAbortWait(slot->owner, ERR_DEAD);
    }

    ObserverNotify(&port->observers);
}

void PortUnref(PortObject *port)
{
    if (!port)
        return;
    if (port->ref_count > 0)
        port->ref_count--;
    if (port->ref_count > 0)
        return;
    PortKill(port);
    ObserverClear(&port->observers);
    PortObjFree(port);
}

bool PortHasPending(const PortObject *port) { return !list_empty(&port->sender_queue); }