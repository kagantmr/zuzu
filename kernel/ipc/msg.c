#include "msg.h"
#include <string.h>
#include <zuzu/tls.h>
#include "kernel/mm/vmm/vmm.h"
#include "kernel/space/space.h"
#include <zuzu/err.h>
#include "core/ensure.h"
#include "core/panic.h"
#include "kernel/sched/sched.h"

#ifdef CONFIG_ZUZU_BENCH

#include "kernel/bench.h"

BENCH_STAT(g_bench_ipc_buf_copy_memcpy, "ipc_buf_copy: memcpy");
BENCH_STAT(g_bench_ipc_buf_copy_wordcopy, "ipc_buf_copy: hand-rolled word-copy");
static uint8_t g_bench_wordcopy_scratch[MSG_BUF_SIZE] __attribute__((aligned(4)));
#endif

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
    if (!src) return ERR_BADHANDLE;
    if (!src->grantable) return ERR_NOPERM;
    return ZUZU_OK;
}


Err AllocateGrantSlot(SpaceObject *from, SpaceObject *to, Handle handle_to_grant, Handle *out)
{
    *out = -1;
    if (handle_to_grant == -1)
        return ZUZU_OK;

    HandleTableEntry *src = HandleTableLookup(&from->handle_table, handle_to_grant);
    if (!src) return ERR_BADHANDLE;
    if (!src->grantable) return ERR_NOPERM;

    Handle new_handle = HandleTableFindFree(&to->handle_table);
    if (new_handle == -1) return ERR_NOMEM;

    HandleTableEntry *dst = HandleTableGet(&to->handle_table, new_handle);
    HandleEntryClaim(&to->handle_table, dst);
    dst->type = src->type;
    dst->grantable = src->grantable;
    dst->mapped_va = src->mapped_va;
    dst->port = src->port;
    dst->marker = src->marker;

    switch (src->type) {
        case HANDLE_PORT:  src->port->ref_count++;  break;
        case HANDLE_EVENT: src->event->ref_count++; break;
        default: break; /* HANDLE_TASK/SPACE/MEM: no shared-refcount concept yet */
    }

    *out = (Handle)HANDLE_PACK(new_handle, dst->generation);
    return ZUZU_OK;
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


void CallBlockAsSender(TaskObject *caller, PortObject *port,
                               EphemeralReplyObject *rc,
                               uint32_t xlen, Handle grant_handle)
{
    caller->ipc_state = IPC_WAITING;
    caller->blocked_port = port;
    caller->pending_reply_cap = rc;
    caller->msg_xfer_len = xlen;
    caller->pending_grant_handle = grant_handle;
    list_add_tail(&caller->node, &port->sender_queue.node);
    caller->state = BLOCKED;
    Schedule();
}

void DeliverCallToReceiver(TaskObject *caller, TaskObject *rx, EphemeralReplyObject *rc,
                            size_t xlen, Handle granted)
{
    CpuState *rx_frame = rx->trap_frame;
    ArchSetInFrame(rx_frame, 0, 0);
    (*ArchGetFromFrame(rx_frame, 1)) = (Register)caller->port_marker;
    (*ArchGetFromFrame(rx_frame, 2)) = (Register)xlen;
    (*ArchGetFromFrame(rx_frame, 3)) = granted;
    if (xlen) MsgBufCopy(caller, rx, xlen);

    rx->reply_cap = rc;
    caller->reply_holder = rx;
}

__hot bool CallHandoffToReceiver(TaskObject *caller, PortObject *port,
                                   EphemeralReplyObject *rc, size_t xlen, Handle grant_handle,
                                   CpuState *frame)
{
    ListNode *node = port->receiver_queue.node.next;
    if (node == &port->receiver_queue.node)
        panic("CallHandoffToReceiver: called with empty receiver_queue (port=%p)", (void *)port);
    WaitSlot *rx_slot = container_of(node, WaitSlot, node);
    TaskObject *rx = rx_slot->owner;
    if (!rx || !rx->trap_frame)
        panic("CallHandoffToReceiver: queued receiver with no trap frame "
              "(port=%p slot=%p owner=%p)", (void *)port, (void *)rx_slot, (void *)rx);

    /* grant_handle was already validated (existence + grantable) in
     * SvcCall before either path was chosen; only allocation in rx's
     * table remains, and can still fail with ERR_NOMEM. */
    Handle granted;
    Err err = AllocateGrantSlot(caller->owner, rx->owner, grant_handle, &granted);
    if (err != ZUZU_OK) {
        ArchSetInFrame(frame, 0, err);
        return false;
    }

    list_remove(node);

    DeliverCallToReceiver(caller, rx, rc, xlen, granted);
    SchedUnblock(rx, WAKE_IPC);

    caller->ipc_state = IPC_WAITING;
    caller->blocked_port = port;
    caller->pending_reply_cap = rc;
    caller->state = BLOCKED;

    if (unlikely(SchedAnyCpuTakers(rx))) {
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
    if (xlen) MsgBufCopy(current_task, target, xlen);

    target->ipc_state = IPC_NONE;
    target->blocked_port = NULL;
    target->pending_reply_cap = NULL;
    target->reply_holder = NULL;
    target->wake_reason = WAKE_IPC;
    target->state = READY;
    SchedAdd(target);
}

void PortReceive(PortObject *port, Duration timeout, CpuState *frame) {
    ENSURE_ERR(frame, port->alive, ERR_DEAD);
    ENSURE_ERR(frame, !current_task->reply_cap, ERR_BUSY);

    while (!list_empty(&port->sender_queue)) {
        ListNode *node = port->sender_queue.node.next;
        TaskObject *caller = container_of(node, TaskObject, node);

        Handle granted;
        Err rc = AllocateGrantSlot(caller->owner, current_task->owner, caller->pending_grant_handle, &granted);
        list_remove(node);
        if (ZUZU_OK != rc) {
            TaskAbortWait(caller, rc);
            continue;
        }

        caller->pending_grant_handle = -1;
        DeliverCallToReceiver(caller, current_task, caller->pending_reply_cap, caller->msg_xfer_len, granted);
        return;

    }
    
    SchedBlockOn(&port->receiver_queue, timeout);
}