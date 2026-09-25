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

void PortDestroy(PortObject *port) {
    ENSURE_GOTO((NULL != port), DestroyPortEnd);
    ENSURE_GOTO(port->alive, DestroyPortEnd);

    if (port->ref_count > 0)
         port->ref_count--;
     if (port->ref_count > 0)
         goto DestroyPortEnd;   // other holders remain so don't tear down yet

    
    // Wake all blocked senders with error
    while (!list_empty(&port->sender_queue))
    {
        ListNode *n = list_pop_front(&port->sender_queue);
        TaskObject *t = container_of(n, TaskObject, node);
        IpcAbortWait(t, ERR_DEAD);
    }

    // Wake all blocked receivers with error
    while (!list_empty(&port->receiver_queue))
    {
        ListNode *n = list_pop_front(&port->receiver_queue);
        WaitSlot *slot = container_of(n, WaitSlot, node);
        TaskObject *t = slot->owner;
        IpcAbortWait(t, ERR_DEAD);
    }

    port->alive = false;

    PortObjFree(port);
DestroyPortEnd:
    return;
}