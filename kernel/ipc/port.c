#include "port.h"
#include "core/ensure.h"
#include "kernel/ipc/msg.h"
#include "kernel/mm/alloc.h"
#include "kernel/space/space.h"
#include "kernel/sched/sched.h"
#include <zuzu/err.h>

static KHeapSlabCache port_cache;

PortObject *PortObjAlloc(void)
{
    if (!port_cache.obj_size)
        KSlabInit(&port_cache, "Port", sizeof(PortObject));
    return KSlabAlloc(&port_cache);
}

void PortObjFree(PortObject *port) { KSlabFree(&port_cache, port); }

PortObject *PortCreate(SpaceObject *owner) {
    PortObject *new_port = PortObjAlloc();
    ENSURE_RET((NULL != new_port), NULL);
    
    list_init(&new_port->sender_queue);
    list_init(&new_port->receiver_queue);
    new_port->owner_spid = owner->spid;
    new_port->ref_count = 1;
    new_port->alive = true;
    new_port->owner = owner;

    return new_port;
}

void PortKill(PortObject *port) {
    if (!port || !port->alive)
        return;
    port->alive = false;

    while (!list_empty(&port->sender_queue))
    {
        ListNode *n = list_pop_front(&port->sender_queue);
        TaskObject *t = container_of(n, TaskObject, node);
        TaskAbortWait(t, ERR_DEAD);
    }

    while (!list_empty(&port->receiver_queue))
    {
        ListNode *n = list_pop_front(&port->receiver_queue);
        WaitSlot *slot = container_of(n, WaitSlot, node);
        TaskAbortWait(slot->owner, ERR_DEAD);
    }
}

void PortDestroy(PortObject *port) {
    if (!port)
        return;
    if (port->ref_count > 0)
        port->ref_count--;
    if (port->ref_count > 0)
        return;
    PortKill(port);
    if (port->bound_ev) {
        port->bound_ev->bind_count--;
        EventDropReference(port->bound_ev);
    }
    PortObjFree(port);
}

void PortMaybeSignalBind(PortObject *port)
{
    if (port->bound_ev && port->bound_ev->alive && !list_empty(&port->sender_queue))
        EventSignal(port->bound_ev, (1U << port->bind_bit), false);
}