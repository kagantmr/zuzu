/**
 * @file msg.h
 * @brief Message-passing function implementation.
 */

#include "msg.h"
#include "core/ensure.h"
#include "core/panic.h"
#include "kernel/mm/vmm/vmm.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"
#include <string.h>
#include <util/tls.h>
#include <zuzu/err.h>

#ifdef CONFIG_ZUZU_BENCH

#include "kernel/bench.h"

BENCH_STAT(g_bench_ipc_buf_copy_memcpy, "ipc_buf_copy: memcpy");
BENCH_STAT(g_bench_ipc_buf_copy_wordcopy, "ipc_buf_copy: hand-rolled word-copy");
static uint8_t g_bench_wordcopy_scratch[MSG_BUF_SIZE] __attribute__((aligned(4)));
#endif /* CONFIG_ZUZU_BENCH */

void __hot MsgBufCopy(TaskObject *restrict src, TaskObject *restrict dst, size_t len)
{
    if (!len || !src->msg_buf_phys_addr || !dst->msg_buf_phys_addr)
        return;
    if (len > MSG_BUF_SIZE)
        return;

    const void *srcp = (const void *)PA_TO_VA(src->msg_buf_phys_addr);
    void *dstp = (void *)PA_TO_VA(dst->msg_buf_phys_addr);

#ifdef CONFIG_ZUZU_BENCH
    uint32_t bench_start = BENCH_BEGIN();
#endif
    memcpy(dstp, srcp, len);
#ifdef CONFIG_ZUZU_BENCH
    BENCH_END(g_bench_ipc_buf_copy_memcpy, bench_start);

    if (((uintptr_t)srcp & 3u) == 0 && (len & 3u) == 0) {
        bench_start = BENCH_BEGIN();
        const uint32_t *ws = (const uint32_t *)srcp;
        uint32_t *wd = (uint32_t *)(void *)g_bench_wordcopy_scratch;
        uint32_t nwords = len / 4u;
        for (uint32_t i = 0; i < nwords; i++)
            wd[i] = ws[i];
        BENCH_END(g_bench_ipc_buf_copy_wordcopy, bench_start);
    }
#endif
}

__hot HandleTableEntry *ValidateCallPort(SpaceObject *space, Handle handle, CpuState *frame)
{
    HandleTableEntry *entry = HandleTableLookup(&space->handle_table, handle);
    ENSURE(entry, ArchSetInFrame(frame, 0, ERR_BADHANDLE); return NULL);
    ENSURE((entry->perms & PERM_SEND), ArchSetInFrame(frame, 0, ERR_NOPERM); return NULL);
    ENSURE((entry->type == HANDLE_PORT), ArchSetInFrame(frame, 0, ERR_BADTYPE); return NULL);
    ENSURE(entry->port, ArchSetInFrame(frame, 0, ERR_BADHANDLE); return NULL);
    ENSURE(entry->port->alive, ArchSetInFrame(frame, 0, ERR_DEAD); return NULL);
    return entry;
}

Err ValidateGrantHandle(SpaceObject *from, Handle handle_to_grant)
{
    if (handle_to_grant == -1)
        return ZUZU_OK;

    HandleTableEntry *src = HandleTableLookup(&from->handle_table, handle_to_grant);
    if (!src)
        return ERR_BADHANDLE;
    if (!(src->perms & PERM_TXFR))
        return ERR_NOPERM;
    return ZUZU_OK;
}

Err AllocateGrantSlot(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out)
{
    *out = -1;
    if (handle_to_grant == -1)
        return ZUZU_OK;
    HandleTableEntry *src = HandleTableLookup(&from->handle_table, handle_to_grant);
    if (!src)
        return ERR_BADHANDLE;
    if (!(src->perms & PERM_TXFR))
        return ERR_NOPERM;
    return HandleCopyInto(&to->handle_table, src, src->perms, src->marker, out);
}

Err GrantHandleAcross(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out)
{
    Err err = ValidateGrantHandle(from, handle_to_grant);
    if (err != ZUZU_OK) {
        *out = -1;
        return err;
    }
    return AllocateGrantSlot(from, to, handle_to_grant, out);
}

/**
 * @brief Arm the timer and insert the caller into the sleep queue.
 * 
 * @param[in] caller The caller to wake upon timeout.
 * @param[in] timeout Duration to arm the timeout on.
 */
static void CallArmTimeout(TaskObject *caller, Duration timeout)
{
    if (timeout == TIMEOUT_INFINITE) {
        caller->wake_deadline = 0;
        return;
    }
    caller->wake_deadline = ArchDeadlineFromMs(timeout);
    SchedInsertSleepQueue(caller);
}

void CallBlockAsSender(TaskObject *caller, PortObject *port, ReplyObject *rc, uint32_t xlen,
                       Handle grant_handle, Duration timeout)
{
    caller->ipc_state = IPC_WAITING;
    caller->blocked_port = port;
    caller->pending_reply_cap = rc;
    caller->msg_xfer_len = xlen;
    caller->pending_grant_handle = grant_handle;
    SchedWaitQueueAddTask(&port->sender_queue, caller);
    caller->state = TASK_STATE_BLOCKED;
    CallArmTimeout(caller, timeout);
    PortBoostServers(port);
    if (PortHasPending(port))
        ObserverNotify(&port->observers);
    Schedule();
}

void DeliverCallToReceiver(TaskObject *caller, TaskObject *rx, ReplyObject *rc, size_t xlen,
                           Handle granted)
{
    CpuState *rx_frame = rx->trap_frame;
    ArchSetInFrame(rx_frame, 0, 0);
    (*ArchGetFromFrame(rx_frame, 1)) = (Register)caller->port_marker;
    (*ArchGetFromFrame(rx_frame, 2)) = (Register)xlen;
    (*ArchGetFromFrame(rx_frame, 3)) = granted;
    if (xlen)
        MsgBufCopy(caller, rx, xlen);

    rx->serving_port = caller->blocked_port;
    ListAddTail(&rx->serve_node, &rx->serving_port->active_servers.node);
    rx->reply_cap = rc;
    caller->reply_holder = rx;
    TaskRecomputePriority(rx);
}

__hot bool CallHandoffToReceiver(TaskObject *caller, PortObject *port, ReplyObject *rc, size_t xlen,
                                 Handle grant_handle, Duration timeout, CpuState *frame)
{
    ListNode *node = port->receiver_queue.node.next;
    if (node == &port->receiver_queue.node)
        panic("CallHandoffToReceiver: called with empty receiver_queue (port=%p)", (void *)port);
    WaitSlot *rx_slot = container_of(node, WaitSlot, node);
    TaskObject *rx = rx_slot->owner;
    if (!rx)
        panic("CallHandoffToReceiver: queued receiver slot with no owner (port=%p slot=%p)",
              (void *)port, (void *)rx_slot);

    Handle granted;
    Err err = AllocateGrantSlot(caller->owner, rx->owner, grant_handle, &granted);
    if (err != ZUZU_OK) {
        ArchSetInFrame(frame, 0, err);
        return false;
    }

    ListRemove(node);

    caller->ipc_state = IPC_WAITING;
    caller->blocked_port = port;
    caller->pending_reply_cap = rc;
    caller->state = TASK_STATE_BLOCKED;
    CallArmTimeout(caller, timeout);

    DeliverCallToReceiver(caller, rx, rc, xlen, granted);
    SchedUnblock(rx);

    if (unlikely(rx->suspended || (rx->owner && rx->owner->frozen) || SchedAnyCpuTakers(rx))) {
        SchedAdd(rx);
        Schedule();
    } else {
        SchedSwitchNext(rx);
    }
    return true;
}

void ReplyDeliverToCaller(TaskObject *target, uint32_t xlen, Handle granted)
{
    CpuState *target_frame = target->trap_frame;
    ArchSetInFrame(target_frame, 0, ZUZU_OK);
    (*ArchGetFromFrame(target_frame, 1)) = (Register)xlen;
    (*ArchGetFromFrame(target_frame, 3)) = granted;
    if (xlen)
        MsgBufCopy(current_task, target, xlen);

    SchedRemoveSleepQueue(target);
    target->wake_deadline = 0;
    target->ipc_state = IPC_NONE;
    target->blocked_port = NULL;
    target->pending_reply_cap = NULL;
    target->reply_holder = NULL;
    target->state = TASK_STATE_READY;
    SchedAdd(target);
}

void ReplyFailCaller(TaskObject *target, Err err)
{
    CpuState *target_frame = target->trap_frame;
    ArchSetInFrame(target_frame, 0, err);
    (*ArchGetFromFrame(target_frame, 1)) = 0;
    (*ArchGetFromFrame(target_frame, 3)) = (Register)-1;

    SchedRemoveSleepQueue(target);
    target->wake_deadline = 0;
    target->ipc_state = IPC_NONE;
    target->blocked_port = NULL;
    target->pending_reply_cap = NULL;
    target->reply_holder = NULL;
    target->state = TASK_STATE_READY;
    SchedAdd(target);
}

void PortReceive(PortObject *port, Duration timeout, CpuState *frame)
{
    ENSURE_ERR(frame, port->alive, ERR_DEAD);
    ENSURE_ERR(frame, !current_task->reply_cap, ERR_BUSY);

    while (!ListIsEmpty(&port->sender_queue)) {
        ListNode *node = port->sender_queue.node.next;
        TaskObject *caller = container_of(node, TaskObject, node);

        Handle granted;
        Err rc = AllocateGrantSlot(caller->owner, current_task->owner, caller->pending_grant_handle,
                                   &granted);
        ListRemove(node);
        if (ZUZU_OK != rc) {
            TaskAbortWait(caller, rc);
            continue;
        }

        caller->pending_grant_handle = -1;
        DeliverCallToReceiver(caller, current_task, caller->pending_reply_cap, caller->msg_xfer_len,
                              granted);
        PortBoostServers(port);
        if (PortHasPending(port))
            ObserverNotify(&port->observers);
        return;
    }

    SchedBlockOn(&port->receiver_queue, timeout);
}

// static uint16_t next_gen removed

Err PortHoldCurrent(TaskObject *server, Ticket *out)
{
    ReplyObject *rc = server->reply_cap;
    PortObject *port = server->serving_port;
    if (!rc || !port)
        return ERR_BADARG;

    TaskObject *caller = rc->caller_task;
    if (!caller || caller->tid != rc->caller_tid || caller->ipc_state != IPC_WAITING) {
        TaskClearReplyCap(server);
        return ERR_DEAD;
    }

    if (!port->held) {
        port->held = KZAlloc(PORT_HELD_SLOTS * sizeof(HeldCall));
        if (!port->held)
            return ERR_NOMEM;
    }
    uint32_t slot = 0;
    while (slot < PORT_HELD_SLOTS && port->held[slot].caller)
        slot++;
    if (slot == PORT_HELD_SLOTS)
        return ERR_NOMEM;

    uint16_t gen = (uint16_t)((port->held[slot].gen + 1U) & 0x7FFFU);
    if (!gen)
        gen = 1;

    port->held[slot] = (HeldCall){.caller = caller, .caller_tid = caller->tid, .gen = gen};
    caller->reply_holder = NULL;
    caller->held_port = port;
    caller->held_slot = (uint8_t)slot;

    *out = TICKET_PACK(HANDLE_INDEX(server->serving_handle), slot, gen);
    TaskClearReplyCap(server);
    return ZUZU_OK;
}

TaskObject *PortTakeHeld(TaskObject *server, Ticket ticket, Err *err)
{
    HandleTableEntry *e =
        HandleTableGet(&server->owner->handle_table, (Handle)TICKET_INDEX(ticket));
    if (!e || e->type != HANDLE_PORT || !e->port) {
        *err = ERR_BADHANDLE;
        return NULL;
    }
    if (!(e->perms & PERM_WAIT)) {
        *err = ERR_NOPERM;
        return NULL;
    }

    PortObject *port = e->port;
    HeldCall *h = port->held ? &port->held[TICKET_SLOT(ticket)] : NULL;
    if (!h || !h->caller || h->gen != TICKET_GEN(ticket)) {
        *err = ERR_NOENT;
        return NULL;
    }

    TaskObject *caller = h->caller;
    if (caller->tid != h->caller_tid || caller->ipc_state != IPC_WAITING) {
        *err = ERR_NOENT;
        return NULL;
    }

    h->caller = NULL;
    caller->held_port = NULL;
    return caller;
}
