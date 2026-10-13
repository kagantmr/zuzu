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

    ListInit(&new_port->sender_queue);
    ListInit(&new_port->receiver_queue);
    ListInit(&new_port->active_servers);
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

    while (!ListIsEmpty(&port->sender_queue)) {
        ListNode *n = ListPopFront(&port->sender_queue);
        TaskObject *t = container_of(n, TaskObject, node);
        TaskAbortWait(t, ERR_DEAD);
    }

    while (!ListIsEmpty(&port->receiver_queue)) {
        ListNode *n = ListPopFront(&port->receiver_queue);
        WaitSlot *slot = container_of(n, WaitSlot, node);
        TaskAbortWait(slot->owner, ERR_DEAD);
    }

    while (!ListIsEmpty(&port->active_servers)) {
        ListNode *n = ListPopFront(&port->active_servers);
        TaskObject *server = container_of(n, TaskObject, serve_node);
        server->serving_port = NULL;
        TaskRecomputePriority(server);
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

bool PortHasPending(const PortObject *port) { return !ListIsEmpty(&port->sender_queue); }