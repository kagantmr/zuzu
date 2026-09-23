#include "svc.h"
#include <arch/regs.h>
#include "core/ensure.h"
#include "kernel/ipc/msg.h"
#include "kernel/space/space.h"
#include "kernel/sched/sched.h"

static __hot HandleTableEntry *ValidateCallPort(SpaceObject *space, Handle handle, CpuState *frame)
{
    HandleTableEntry *entry = HandleTableLookup(&space->handle_table, handle);
    ENSURE(entry, ArchSetInFrame(frame, 0, ERR_BADHANDLE); return NULL);
    ENSURE((entry->type == HANDLE_PORT), ArchSetInFrame(frame, 0, ERR_BADTYPE); return NULL);
    ENSURE(entry->port, ArchSetInFrame(frame, 0, ERR_BADHANDLE); return NULL);
    ENSURE(entry->port->alive, ArchSetInFrame(frame, 0, ERR_DEAD); return NULL);
    return entry;
}

static Handle GrantHandleAcross(SpaceObject *from, SpaceObject *to,
                                  Handle handle_to_grant, CpuState *frame)
{
    if (handle_to_grant == 0)
        return 0;

    HandleTableEntry *src = HandleTableLookup(&from->handle_table, handle_to_grant);
    ENSURE(src, ArchSetInFrame(frame, 0, ERR_BADHANDLE); return -1);
    ENSURE(src->grantable, ArchSetInFrame(frame, 0, ERR_NOPERM); return -1);

    Handle new_handle = HandleTableFindFree(&to->handle_table);
    ENSURE((-1 != new_handle), ArchSetInFrame(frame, 0, ERR_NOMEM); return -1);

    HandleTableEntry *dst = HandleTableGet(&to->handle_table, new_handle);
    HandleEntryClaim(&to->handle_table, dst);
    dst->type = src->type;
    dst->grantable = src->grantable;
    dst->memtype = src->memtype;
    dst->mapped_va = src->mapped_va;
    dst->port = src->port; /* union: one write covers every member */

    switch (src->type) {
        case HANDLE_PORT:  src->port->ref_count++;  break;
        case HANDLE_EVENT: src->event->ref_count++; break;
        default: break; /* HANDLE_TASK/SPACE/MEM: no shared-refcount concept yet */
    }

    return (Handle)HANDLE_PACK(new_handle, dst->generation);
}


static void CallBlockAsSender(TaskObject *caller, PortObject *port,
                               HandleTableEntry *entry, EphemeralReplyObject *rc, uint32_t xlen)
{
    caller->ipc_state = IPC_WAITING;
    caller->blocked_port = port;
    caller->pending_reply_cap = rc;
    caller->port_marker = entry->marker;
    caller->lmsg_buf_xfer_len = xlen;
    list_add_tail(&caller->node, &port->sender_queue.node);
    caller->state = BLOCKED;
    Schedule();
}

static __hot bool CallHandoffToReceiver(TaskObject *caller, PortObject *port,
                                   EphemeralReplyObject *rc, size_t xlen, Handle grant_handle,
                                   CpuState *frame)
{
    ListNode *node = list_pop_front(&port->receiver_queue);
    WaitSlot *rx_slot = container_of(node, WaitSlot, node);
    TaskObject *rx = rx_slot->owner;
    CpuState *rx_frame = rx->trap_frame;

    int32_t granted = GrantHandleAcross(caller->owner, rx->owner, grant_handle, frame);
    if (granted < 0) {
        list_add_tail(&rx_slot->node, &port->receiver_queue.node); /* grant failed, put it back */
        return false;
    }
    (*ArchGetFromFrame(frame, 3)) = granted;

    rc->holder = rx->owner;
    rc->holder_spid = rx->owner->spid;
    rx->reply_cap = rc;

    ArchSetInFrame(rx_frame, 0, caller->owner->spid);
    (*ArchGetFromFrame(rx_frame, 1)) = (Register)xlen;
    if (xlen) MsgBufCopy(caller, rx, xlen);

    rx->ipc_state = IPC_NONE;
    rx->blocked_port = NULL;
    rx->wake_reason = WAKE_IPC;

    caller->ipc_state = IPC_WAITING;
    caller->blocked_port = port;
    caller->pending_reply_cap = rc;
    caller->state = BLOCKED;
    caller->pending_grant_handle = grant_handle;

    if (unlikely(SchedAnyCpuTakers(rx))) {
        rx->state = READY;
        SchedAdd(rx);
        Schedule();
    } else {
        SchedSwitchNext(rx);
    }
    return true;
}

void __hot SvcCall(CpuState *frame)
{
    Handle handle = (Handle)(*ArchGetFromFrame(frame, 0));
    uint32_t xlen = (uint32_t)(*ArchGetFromFrame(frame, 1));
    Handle grant_handle = (Handle)(*ArchGetFromFrame(frame, 2));
    ENSURE_ERR(frame, (xlen <= MSG_BUF_SIZE), ERR_OVERFLOW);

    HandleTableEntry *entry = ValidateCallPort(CURRENT_SPACE, handle, frame);
    if (!entry) return;
    PortObject *port = entry->port;

    EphemeralReplyObject *rc = &current_task->reply_cap_storage;
    rc->caller = CURRENT_SPACE;
    rc->caller_tid = current_task->tid;

    (*ArchGetFromFrame(frame, 2)) = (Register)entry->marker;

    if (!list_empty(&port->receiver_queue)) {
        if (!CallHandoffToReceiver(current_task, port, rc, xlen, grant_handle, frame)) {
            return;
        }
    } else {
        CallBlockAsSender(current_task, port, entry, rc, xlen);
    }
}
