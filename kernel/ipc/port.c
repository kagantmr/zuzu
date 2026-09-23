#include "port.h"
#include "core/ensure.h"
#include "kernel/space/space.h"
#include "kernel/sched/sched.h"
#include <zuzu/err.h>

PortObject *PortCreate(SpaceObject *owner) {
    PortObject *new_port = (PortObject *)PortObjAlloc();
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
        t->ipc_state = IPC_NONE;
        t->blocked_port = NULL;
        if (t->trap_frame)
            arch_reg_set(t->trap_frame, 0, ERR_DEAD);
        t->wake_reason = WAKE_IPC;
        t->state = READY;
        SchedAdd(t);
    }

    // Wake all blocked receivers with error
    while (!list_empty(&port->receiver_queue))
    {
        ListNode *n = list_pop_front(&port->receiver_queue);
        WaitSlot *slot = container_of(n, WaitSlot, node);
        TaskObject *t = slot->owner;
        t->ipc_state = IPC_NONE;
        t->blocked_port = NULL;
        if (t->trap_frame)
            arch_reg_set(t->trap_frame, 0, ERR_DEAD);
        SchedRemoveSleepQueue(t);
        t->wake_deadline = 0;
        t->wake_reason = WAKE_IPC;
        t->state = READY;
        SchedAdd(t);
    }

    port->alive = false;

    PortObjFree(port);
DestroyPortEnd:
    return;
}