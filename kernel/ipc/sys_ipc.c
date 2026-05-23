#include "sys_ipc.h"
#include "kernel/syscall/syscall.h"
#include "endpoint.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/kstack.h"
#include "kernel/layout.h"
#include "kernel/time/tick.h"
#include "core/panic.h"
#include <mem.h>
#include <stdbool.h>
#include <zuzu/ipcx.h>
#include <zuzu/types.h>

#include "kernel/irq/sys_irq.h"

#define LOG_FMT(fmt) "(ipc) " fmt
#include "core/log.h"

#define KSTACK_REGION_TOP (KSTACK_REGION_BASE + (64u * 0x2000u))
#define RECVANY_MAX_HANDLES 16u
#define RECVANY_KIND_SEND 0u
#define RECVANY_KIND_CALL 1u
#define RECVANY_KIND_IRQ 2u
#define RECVANY_KIND_TIMEOUT 3u

extern thread_t *current_thread;
extern list_head_t sleep_queue;
extern kernel_layout_t kernel_layout;


static void ipc_buf_copy(thread_t *src, thread_t *dst, uint32_t len)
{
    if (!len || !src->ipc_buf_pa || !dst->ipc_buf_pa) return;
    if (len > IPCX_BUF_SIZE) len = IPCX_BUF_SIZE;
    memcpy((void *)PA_TO_VA(dst->ipc_buf_pa),
           (void *)PA_TO_VA(src->ipc_buf_pa), len);
}

static bool trap_frame_sane(const exception_frame_t *tf)
{
    uintptr_t p = (uintptr_t)tf;
    if (p == 0 || (p & 0x3u) != 0)
        return false;

    if (kernel_layout.stack_base_va && kernel_layout.stack_top_va &&
        p >= kernel_layout.stack_base_va &&
        p + sizeof(exception_frame_t) <= kernel_layout.stack_top_va)
        return true;

    if (p >= KSTACK_REGION_BASE && p + sizeof(exception_frame_t) <= KSTACK_REGION_TOP)
        return true;

    return false;
}

static void ipc_panic_bad_trap_frame(const char *where, const process_t *owner, const exception_frame_t *tf)
{
    KERROR("bad trap_frame at %s: owner_pid=%u tf=%p current_pid=%u", where,
           owner ? owner->pid : 0u,
           tf,
           current_thread && current_thread->owner_process ? current_thread->owner_process->pid : 0u);
    panic("Corrupt trap_frame pointer in IPC path");
}

static void ipc_cancel_timeout(thread_t *t)
{
    if (t->wake_tick != 0 &&
        t->timeout_node.prev && t->timeout_node.next) {
        list_remove(&t->timeout_node);
    }
    t->wake_tick = 0;
}

static void ipc_wake_ready(thread_t *t)
{
    t->ipc_state = IPC_NONE;
    t->blocked_endpoint = NULL;
    ipc_cancel_timeout(t);
    t->wake_reason = WAKE_IPC;
    t->state = READY;
    sched_add(t);
}

static endpoint_t *validate_endpoint_handle(process_t *proc, handle_t handle, exception_frame_t *frame)
{
    if (!proc)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }

    handle_entry_t *entry = handle_vec_get(&proc->handle_table, handle);
    if (!entry)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }
    if (entry->type != HANDLE_ENDPOINT)
    {
        frame->r[0] = ERR_NOPERM;
        return NULL;
    }
    if (!entry->ep)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }
    if (!entry->ep->alive)
    {
        frame->r[0] = ERR_DEAD;
        return NULL;
    }

    return entry->ep;
}

static notification_t *validate_notification_handle(process_t *proc, handle_t handle, exception_frame_t *frame)
{
    if (!proc)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }

    handle_entry_t *entry = handle_vec_get(&proc->handle_table, handle);
    if (!entry)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }
    if (entry->type != HANDLE_NOTIFICATION)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }
    if (!entry->ntfn)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }
    if (!entry->ntfn->alive)
    {
        frame->r[0] = ERR_DEAD;
        return NULL;
    }

    return entry->ntfn;
}

static handle_entry_t *validate_reply_handle(process_t *proc,
                                             handle_t handle_idx,
                                             thread_t **target_out,
                                             exception_frame_t *frame)
{
    if (!proc || handle_idx == 0)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }

    handle_entry_t *entry = handle_vec_get(&proc->handle_table, handle_idx);
    if (!entry)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }
    if (entry->type != HANDLE_REPLY)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }
    if (!entry->reply || entry->reply->caller_tid == 0)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }

    thread_t *target = thread_find_by_tid(entry->reply->caller_tid);

    if (!target || target->state == ZOMBIE)
    {
        process_untrack_reply_cap(entry->reply);
        kfree_reply_cap(entry->reply);
        entry->reply = NULL;
        entry->grantable = false;
        entry->type = HANDLE_FREE;
        frame->r[0] = ERR_DEAD;
        return NULL;
    }

    if (target->ipc_state != IPC_WAITING)
    {
        process_untrack_reply_cap(entry->reply);
        kfree_reply_cap(entry->reply);
        entry->reply = NULL;
        entry->grantable = false;
        entry->type = HANDLE_FREE;
        frame->r[0] = ERR_DEAD;
        return NULL;
    }

    *target_out = target;
    return entry;
}

static void recvany_deliver_notification(uint32_t matched_index,
                                         uint32_t bits,
                                         recvany_result_t *result)
{
    memset(result, 0, sizeof(*result));
    result->matched_index = matched_index;
    result->kind = RECVANY_KIND_IRQ;
    result->source = bits;
    result->r1 = bits;
}

void proc_send(exception_frame_t *frame)
{
    int handle = (int)frame->r[0];

    endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!ep)
    {
        return;
    }

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        thread_t *rx_thread = container_of(receiver, thread_t, node);
        exception_frame_t *rx_frame = rx_thread->trap_frame;
        if (!trap_frame_sane(rx_frame))
        {
            ipc_panic_bad_trap_frame("proc_send.rx", rx_thread->owner_process, rx_frame);
        }
        // KDEBUG("Sending message from thread %d to thread %d", current_thread->tid, rx_thread->tid);
        rx_frame->r[0] = current_thread->owner_process->pid;
        rx_frame->r[1] = frame->r[1];
        rx_frame->r[2] = frame->r[2];
        rx_frame->r[3] = frame->r[3];
        rx_thread->ipc_state = IPC_NONE;
        rx_thread->blocked_endpoint = NULL;
        // Cancel timeout if receiver had one
        if (rx_thread->wake_tick != 0 &&
            rx_thread->timeout_node.prev && rx_thread->timeout_node.next) {
            list_remove(&rx_thread->timeout_node);
        }
        rx_thread->wake_tick = 0;
        rx_thread->wake_reason = WAKE_IPC;
        rx_thread->state = READY;
        sched_add(rx_thread);
        frame->r[0] = 0;
    }
    else
    {
        current_thread->ipc_state = IPC_SENDER;
        current_thread->blocked_endpoint = ep;
        list_add_tail(&current_thread->node, &ep->sender_queue.node);
        current_thread->state = BLOCKED;
        schedule();
    }
}

void proc_recv(exception_frame_t *frame)
{
    int handle = (int)frame->r[0];
    uint32_t timeout_ms = frame->r[1]; // 0 = infinite (backward compatible)

    endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!ep)
    {
        return;
    }

    if (!list_empty(&ep->sender_queue))
    {
        // KDEBUG("sender queue NOT empty");
        list_node_t *sender = list_pop_front(&ep->sender_queue);
        thread_t *sr_thread = container_of(sender, thread_t, node);
        exception_frame_t *sr_frame = sr_thread->trap_frame;
        if (!trap_frame_sane(sr_frame))
        {
            ipc_panic_bad_trap_frame("proc_recv.sr", sr_thread->owner_process, sr_frame);
        }

        // Copy message to receiver
        // KDEBUG("Got message from thread %d as thread %d", sr_thread->tid, current_thread->tid);
        frame->r[0] = sr_thread->owner_process->pid;
        frame->r[1] = sr_frame->r[1];
        frame->r[2] = sr_frame->r[2];
        frame->r[3] = sr_frame->r[3];

        if (sr_thread->ipc_state == IPC_SENDER)
        {
            // wake the sender, it's done
            sr_frame->r[0] = 0;
            sr_thread->ipc_state = IPC_NONE;
            sr_thread->blocked_endpoint = NULL;
            // Cancel timeout if sender had one
            if (sr_thread->wake_tick != 0 &&
                sr_thread->timeout_node.prev && sr_thread->timeout_node.next) {
                list_remove(&sr_thread->timeout_node);
            }
            sr_thread->wake_tick = 0;
            sr_thread->wake_reason = WAKE_IPC;
            sr_thread->state = READY;
            if (sr_thread->ipc_buf_xfer_len > 0) {
                ipc_buf_copy(sr_thread, current_thread, sr_thread->ipc_buf_xfer_len);
                frame->r[1] = sr_thread->ipc_buf_xfer_len;
                frame->r[2] = 0;
                frame->r[3] = 0;
                sr_thread->ipc_buf_xfer_len = 0;
            }
            sched_add(sr_thread);
        }
        else if (sr_thread->ipc_state == IPC_WAITING)
        {
            // Use the pre-allocated reply cap
            reply_cap_t *rc = sr_thread->pending_reply_cap;
            sr_thread->pending_reply_cap = NULL;
            // rc is guaranteed non-NULL — caller pre-allocated it

            int slot = handle_vec_find_free(&current_thread->owner_process->handle_table);
            if (slot < 0) {
                // Handle table full - but at least we can report the error
                // and the caller's rc gets cleaned up
                kfree_reply_cap(rc);
                sr_thread->pending_reply_cap = NULL;
                // Wake the caller with an error instead of leaving it stuck
                sr_thread->trap_frame->r[0] = ERR_NOMEM;
                sr_thread->ipc_state = IPC_NONE;
                sr_thread->blocked_endpoint = NULL;
                // Cancel timeout if sender had one
                if (sr_thread->wake_tick != 0 &&
                    sr_thread->timeout_node.prev && sr_thread->timeout_node.next) {
                    list_remove(&sr_thread->timeout_node);
                }
                sr_thread->wake_tick = 0;
                sr_thread->wake_reason = WAKE_IPC;
                sr_thread->state = READY;
                sched_add(sr_thread);
                frame->r[0] = ERR_NOMEM;
                return;
            }

            handle_entry_t *rentry = handle_vec_get(&current_thread->owner_process->handle_table, slot);
            rentry->type = HANDLE_REPLY;
            rentry->grantable = false;
            rentry->reply = rc;
            process_track_reply_cap(sr_thread->owner_process, current_thread->owner_process, (uint32_t)slot, rc);

            frame->r[0] = slot;
            frame->r[1] = sr_thread->owner_process->pid;
            frame->r[2] = sr_frame->r[1];
            frame->r[3] = sr_frame->r[2];
            if (sr_thread->ipc_buf_xfer_len > 0) {
                ipc_buf_copy(sr_thread, current_thread, sr_thread->ipc_buf_xfer_len);
                frame->r[2] = sr_thread->ipc_buf_xfer_len;
                frame->r[3] = 0;
                sr_thread->ipc_buf_xfer_len = 0;
            }
        }
    }
    else
    {
        if (timeout_ms == UINT32_MAX)
        {
            frame->r[0] = ERR_BUSY;
            return;
        }

        current_thread->ipc_state = IPC_RECEIVER;
        current_thread->blocked_endpoint = ep;
        current_thread->wake_reason = WAKE_NONE;
        list_add_tail(&current_thread->node, &ep->receiver_queue.node);
        current_thread->state = BLOCKED;

        if (timeout_ms > 0)
        {
            tick_t ticks = ((uint64_t)timeout_ms * (uint64_t)TICK_HZ) / 1000u;
            if (ticks == 0)
                ticks = 1;
            current_thread->wake_tick = get_ticks() + ticks;
            sleep_queue_insert(current_thread);
        }
        else
        {
            current_thread->wake_tick = 0;
        }

        schedule();

        if (timeout_ms > 0 && current_thread->wake_reason != WAKE_TIMEOUT &&
            current_thread->timeout_node.prev && current_thread->timeout_node.next)
        {
            list_remove(&current_thread->timeout_node);
        }

        if (current_thread->wake_reason == WAKE_TIMEOUT)
        {
            frame->r[0] = ERR_BUSY;
        }
        // KDEBUG("Listener woke from recv, PID %d", current_process->pid);
    }
}

void proc_call(exception_frame_t *frame)
{
    int handle = (int)frame->r[0];

    endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!ep)
    {
        return;
    }

    reply_cap_t *rc = kalloc_reply_cap();
    if (!rc) {
        frame->r[0] = ERR_NOMEM;
        return;  // caller gets clean error, never blocked
    }
    rc->caller_tid = current_thread ? current_thread->tid : 0;

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        thread_t *rx_thread = container_of(receiver, thread_t, node);
        exception_frame_t *rx_frame = rx_thread->trap_frame;
        if (!trap_frame_sane(rx_frame))
        {
            ipc_panic_bad_trap_frame("proc_call.rx", rx_thread->owner_process, rx_frame);
        }
        // Use pre-allocated rc from this call
        int slot = handle_vec_find_free(&rx_thread->owner_process->handle_table);
        if (slot < 0)
        {
            kfree_reply_cap(rc);
            list_add_tail(&rx_thread->node, &ep->receiver_queue.node);
            frame->r[0] = ERR_NOMEM;
            return;
        }

        handle_entry_t *rentry = handle_vec_get(&rx_thread->owner_process->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;
        process_track_reply_cap(current_thread->owner_process, rx_thread->owner_process, (uint32_t)slot, rc);

        rx_frame->r[0] = slot;
        rx_frame->r[1] = current_thread->owner_process->pid;
        rx_frame->r[2] = frame->r[1];
        rx_frame->r[3] = frame->r[2];

        rx_thread->ipc_state = IPC_NONE;
        rx_thread->blocked_endpoint = NULL;
        // Cancel timeout if receiver had one
        if (rx_thread->wake_tick != 0 &&
            rx_thread->timeout_node.prev && rx_thread->timeout_node.next) {
            list_remove(&rx_thread->timeout_node);
        }
        rx_thread->wake_tick = 0;
        rx_thread->wake_reason = WAKE_IPC;
        rx_thread->state = READY;
        sched_add(rx_thread);

        current_thread->state = BLOCKED;
        current_thread->blocked_endpoint = ep;
        current_thread->ipc_state = IPC_WAITING;
        schedule();
    }
    else
    {
        current_thread->ipc_state = IPC_WAITING;
        current_thread->blocked_endpoint = ep;
        current_thread->pending_reply_cap = rc;
        list_add_tail(&current_thread->node, &ep->sender_queue.node);
        current_thread->state = BLOCKED;
        schedule();
    }
}

void proc_reply(exception_frame_t *frame)
{
    handle_t handle_idx = frame->r[0];
    thread_t *target_thread = NULL;
    handle_entry_t *entry = validate_reply_handle(current_thread->owner_process, handle_idx, &target_thread, frame);
    if (!entry)
    {
        return;
    }

    // Deliver reply into target's saved frame

    exception_frame_t *target_frame = target_thread->trap_frame;
    if (!trap_frame_sane(target_frame))
    {
        ipc_panic_bad_trap_frame("proc_reply.target", target_thread->owner_process, target_frame);
    }
    target_frame->r[0] = 0;           // success
    target_frame->r[1] = frame->r[1]; // reply payload
    target_frame->r[2] = frame->r[2];
    target_frame->r[3] = frame->r[3];

    // Wake the caller
    target_thread->ipc_state = IPC_NONE;
    target_thread->blocked_endpoint = NULL;
    // Cancel timeout if target had one
    if (target_thread->wake_tick != 0 &&
        target_thread->timeout_node.prev && target_thread->timeout_node.next) {
        list_remove(&target_thread->timeout_node);
    }
    target_thread->wake_tick = 0;
    target_thread->wake_reason = WAKE_IPC;
    target_thread->state = READY;
    sched_add(target_thread);

    process_untrack_reply_cap(entry->reply);
    kfree_reply_cap(entry->reply);
    entry->reply = NULL;
    entry->grantable = false;
    entry->type = HANDLE_FREE;
    frame->r[0] = 0;
}

void proc_sendx(exception_frame_t *frame)
{
    int handle = (int)frame->r[0];

    endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!ep)
    {
        return;
    }

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        thread_t *rx_thread = container_of(receiver, thread_t, node);
        exception_frame_t *rx_frame = rx_thread->trap_frame;
        if (!trap_frame_sane(rx_frame))
        {
            ipc_panic_bad_trap_frame("proc_sendx.rx", rx_thread->owner_process, rx_frame);
        }
        // KDEBUG("Sending message from thread %d to thread %d", current_thread->tid, rx_thread->tid);
        uint32_t xlen = frame->r[1] > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : frame->r[1];
        rx_frame->r[0] = current_thread->owner_process->pid;
        rx_frame->r[1] = xlen;
        rx_frame->r[2] = 0;
        rx_frame->r[3] = 0;
        ipc_buf_copy(current_thread, rx_thread, xlen);
        rx_thread->ipc_state = IPC_NONE;
        rx_thread->blocked_endpoint = NULL;
        // Cancel timeout if receiver had one
        if (rx_thread->wake_tick != 0 &&
            rx_thread->timeout_node.prev && rx_thread->timeout_node.next) {
            list_remove(&rx_thread->timeout_node);
        }
        rx_thread->wake_tick = 0;
        rx_thread->wake_reason = WAKE_IPC;
        rx_thread->state = READY;
        sched_add(rx_thread);
        frame->r[0] = 0;
    }
    else
    {
        current_thread->ipc_state = IPC_SENDER;
        current_thread->blocked_endpoint = ep;
        list_add_tail(&current_thread->node, &ep->sender_queue.node);
        current_thread->ipc_buf_xfer_len = frame->r[1] > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : frame->r[1];
        current_thread->state = BLOCKED;
        schedule();
    }
}

void proc_callx(exception_frame_t *frame)
{
    handle_t handle = (handle_t)frame->r[0];

    endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!ep)
    {
        return;
    }

    reply_cap_t *rc = kalloc_reply_cap();
    if (!rc) {
        frame->r[0] = ERR_NOMEM;
        return;  // caller gets clean error, never blocked
    }
    rc->caller_tid = current_thread ? current_thread->tid : 0;

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        thread_t *rx_thread = container_of(receiver, thread_t, node);
        exception_frame_t *rx_frame = rx_thread->trap_frame;
        if (!trap_frame_sane(rx_frame))
        {
            ipc_panic_bad_trap_frame("proc_callx.rx", rx_thread->owner_process, rx_frame);
        }
        // Use pre-allocated rc from this call
        int slot = handle_vec_find_free(&rx_thread->owner_process->handle_table);
        if (slot < 0)
        {
            kfree_reply_cap(rc);
            list_add_tail(&rx_thread->node, &ep->receiver_queue.node);
            frame->r[0] = ERR_NOMEM;
            return;
        }

        handle_entry_t *rentry = handle_vec_get(&rx_thread->owner_process->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;
        process_track_reply_cap(current_thread->owner_process, rx_thread->owner_process, (uint32_t)slot, rc);

        size_t xlen = frame->r[1] > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : frame->r[1];
        rx_frame->r[0] = slot;
        rx_frame->r[1] = current_thread->owner_process->pid;
        rx_frame->r[2] = xlen;
        rx_frame->r[3] = 0;
        ipc_buf_copy(current_thread, rx_thread, xlen);

        rx_thread->ipc_state = IPC_NONE;
        rx_thread->blocked_endpoint = NULL;
        // Cancel timeout if receiver had one
        if (rx_thread->wake_tick != 0 &&
            rx_thread->timeout_node.prev && rx_thread->timeout_node.next) {
            list_remove(&rx_thread->timeout_node);
        }
        rx_thread->wake_tick = 0;
        rx_thread->wake_reason = WAKE_IPC;
        rx_thread->state = READY;
        sched_add(rx_thread);

        current_thread->state = BLOCKED;
        current_thread->blocked_endpoint = ep;
        current_thread->ipc_state = IPC_WAITING;
        schedule();
    }
    else
    {
        current_thread->ipc_state = IPC_WAITING;
        current_thread->blocked_endpoint = ep;
        current_thread->pending_reply_cap = rc;
        list_add_tail(&current_thread->node, &ep->sender_queue.node);
        current_thread->ipc_buf_xfer_len = frame->r[1] > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : frame->r[1];
        current_thread->state = BLOCKED;
        schedule();
    }
}

void proc_replyx(exception_frame_t *frame)
{
    handle_t handle_idx = frame->r[0];
    thread_t *target_thread = NULL;
    handle_entry_t *entry = validate_reply_handle(current_thread->owner_process, handle_idx, &target_thread, frame);
    if (!entry)
    {
        return;
    }

    // Deliver reply into target's saved frame

    exception_frame_t *target_frame = target_thread->trap_frame;
    if (!trap_frame_sane(target_frame))
    {
        ipc_panic_bad_trap_frame("proc_replyx.target", target_thread->owner_process, target_frame);
    }
    size_t xlen = frame->r[1] > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : frame->r[1];
    target_frame->r[0] = 0;           // success
    target_frame->r[1] = xlen;        // reply payload
    target_frame->r[2] = 0;
    target_frame->r[3] = 0;
    ipc_buf_copy(current_thread, target_thread, xlen);

    // Wake the caller
    target_thread->ipc_state = IPC_NONE;
    target_thread->blocked_endpoint = NULL;
    // Cancel timeout if target had one
    if (target_thread->wake_tick != 0 &&
        target_thread->timeout_node.prev && target_thread->timeout_node.next) {
        list_remove(&target_thread->timeout_node);
    }
    target_thread->wake_tick = 0;
    target_thread->wake_reason = WAKE_IPC;
    target_thread->state = READY;
    sched_add(target_thread);

    process_untrack_reply_cap(entry->reply);
    kfree_reply_cap(entry->reply);
    entry->reply = NULL;
    entry->grantable = false;
    entry->type = HANDLE_FREE;
    frame->r[0] = 0;
}

static int recvany_deliver_sender(uint32_t matched_index,
                                  thread_t *receiver,
                                  list_node_t *sender_node,
                                  recvany_result_t *result)
{
    thread_t *sr_thread = container_of(sender_node, thread_t, node);
    exception_frame_t *sr_frame = sr_thread->trap_frame;
    if (!trap_frame_sane(sr_frame))
    {
        ipc_panic_bad_trap_frame("proc_recvany.sr", sr_thread->owner_process, sr_frame);
    }

    memset(result, 0, sizeof(*result));
    result->matched_index = matched_index;

    if (sr_thread->ipc_state == IPC_SENDER)
    {
        result->kind = RECVANY_KIND_SEND;
        result->source = sr_thread->owner_process->pid;
        result->r1 = sr_frame->r[1];
        result->r2 = sr_frame->r[2];
        result->r3 = sr_frame->r[3];

        sr_frame->r[0] = 0;
        sr_thread->ipc_state = IPC_NONE;
        sr_thread->blocked_endpoint = NULL;
        ipc_cancel_timeout(sr_thread);
        sr_thread->wake_reason = WAKE_IPC;
        sr_thread->state = READY;

        if (sr_thread->ipc_buf_xfer_len > 0) {
            ipc_buf_copy(sr_thread, receiver, sr_thread->ipc_buf_xfer_len);
            result->r1 = sr_thread->ipc_buf_xfer_len;
            result->r2 = 0;
            result->r3 = 0;
            sr_thread->ipc_buf_xfer_len = 0;
        }

        sched_add(sr_thread);
        return 0;
    }

    if (sr_thread->ipc_state == IPC_WAITING)
    {
        reply_cap_t *rc = sr_thread->pending_reply_cap;
        sr_thread->pending_reply_cap = NULL;

        int slot = handle_vec_find_free(&receiver->owner_process->handle_table);
        if (slot < 0) {
            kfree_reply_cap(rc);
            sr_frame->r[0] = ERR_NOMEM;
            ipc_wake_ready(sr_thread);
            return ERR_NOMEM;
        }

        handle_entry_t *rentry = handle_vec_get(&receiver->owner_process->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;
        process_track_reply_cap(sr_thread->owner_process, receiver->owner_process, (uint32_t)slot, rc);

        result->kind = RECVANY_KIND_CALL;
        result->source = (uint32_t)slot;
        result->r1 = sr_thread->owner_process->pid;
        result->r2 = sr_frame->r[1];
        result->r3 = sr_frame->r[2];

        if (sr_thread->ipc_buf_xfer_len > 0) {
            ipc_buf_copy(sr_thread, receiver, sr_thread->ipc_buf_xfer_len);
            result->r2 = sr_thread->ipc_buf_xfer_len;
            result->r3 = 0;
            sr_thread->ipc_buf_xfer_len = 0;
        }

        return 0;
    }

    return ERR_BADARG;
}

static int recvany_try_once(const handle_t *handles,
                            uint32_t count,
                            recvany_result_t *result,
                            notification_t **wait_ntfns,
                            uint32_t *wait_indices,
                            uint32_t *wait_count_out)
{
    endpoint_t *endpoints[RECVANY_MAX_HANDLES];
    notification_t *notifications[RECVANY_MAX_HANDLES];

    if (wait_count_out)
        *wait_count_out = 0;

    for (uint32_t i = 0; i < count; i++) {
        handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handles[i]);
        if (!entry) {
            current_thread->trap_frame->r[0] = ERR_BADARG;
            return ERR_BADARG;
        }

        if (entry->type == HANDLE_ENDPOINT) {
            endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handles[i], current_thread->trap_frame);
            if (!ep) {
                return (int)current_thread->trap_frame->r[0];
            }
            endpoints[i] = ep;
            notifications[i] = NULL;
            continue;
        }

        if (entry->type == HANDLE_NOTIFICATION) {
            notification_t *ntfn = validate_notification_handle(current_thread->owner_process, handles[i], current_thread->trap_frame);
            if (!ntfn) {
                return (int)current_thread->trap_frame->r[0];
            }
            endpoints[i] = NULL;
            notifications[i] = ntfn;
            continue;
        }

        current_thread->trap_frame->r[0] = ERR_BADARG;
        return ERR_BADARG;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (endpoints[i] && !list_empty(&endpoints[i]->sender_queue)) {
            list_node_t *sender = list_pop_front(&endpoints[i]->sender_queue);
            return recvany_deliver_sender(i, current_thread, sender, result);
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        notification_t *ntfn = notifications[i];
        if (ntfn && ntfn->word != 0) {
            uint32_t bits = ntfn->word;
            ntfn->word = 0;
            recvany_deliver_notification(i, bits, result);
            return 0;
        }
    }

    uint32_t notif_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (notifications[i]) {
            notif_count++;
            uint32_t slot = notif_count - 1u;
            if (wait_ntfns)
                wait_ntfns[slot] = notifications[i];
            if (wait_indices)
                wait_indices[slot] = i;
        }
    }

    if (wait_count_out)
        *wait_count_out = notif_count;

    return ERR_BUSY;
}

static bool recvany_write_timeout_result(uintptr_t result_ptr)
{
    recvany_result_t result;
    memset(&result, 0, sizeof(result));
    result.matched_index = UINT32_MAX;
    result.kind = RECVANY_KIND_TIMEOUT;
    return copy_to_user((void *)result_ptr, &result, sizeof(result));
}

void proc_recvany(exception_frame_t *frame)
{
    /* r0 = handle array pointer
     * r1 = count
     * r2 = timeout_ms
     * r3 = result struct pointer
     */
    uintptr_t handles_ptr = (uintptr_t)frame->r[0];
    uint32_t count = frame->r[1];
    uint32_t timeout_ms = frame->r[2];
    uintptr_t result_ptr = (uintptr_t)frame->r[3];

    if (!current_thread || !handles_ptr || !result_ptr ||
        count == 0 || count > RECVANY_MAX_HANDLES) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    if (!validate_user_ptr(result_ptr, sizeof(recvany_result_t)) ||
        !fault_in_pages(current_thread->owner_process->as, result_ptr, sizeof(recvany_result_t), true)) {
        frame->r[0] = ERR_BADPTR;
        return;
    }

    handle_t handles_local[RECVANY_MAX_HANDLES];
    size_t copy_size = count * sizeof(handle_t);
    if (!copy_from_user(handles_local, (const void *)handles_ptr, copy_size)) {
        frame->r[0] = ERR_BADPTR;
        return;
    }

    tick_t deadline = 0;
    if (timeout_ms > 0 && timeout_ms != UINT32_MAX) {
        tick_t ticks = ((uint64_t)timeout_ms * (uint64_t)TICK_HZ) / 1000u;
        if (ticks == 0)
            ticks = 1;
        deadline = get_ticks() + ticks;
    }

    for (;;) {
        notification_t *wait_ntfns[RECVANY_MAX_HANDLES];
        uint32_t wait_indices[RECVANY_MAX_HANDLES];
        uint32_t wait_count = 0;
        recvany_result_t result;
        int err = recvany_try_once(handles_local, count, &result, wait_ntfns, wait_indices, &wait_count);
        if (err == 0) {
            if (!copy_to_user((void *)result_ptr, &result, sizeof(result))) {
                frame->r[0] = ERR_BADPTR;
                return;
            }
            frame->r[0] = 0;
            return;
        }

        if (err != ERR_BUSY) {
            frame->r[0] = err;
            return;
        }

        if (timeout_ms == UINT32_MAX) {
            frame->r[0] = ERR_BUSY;
            return;
        }

        if (wait_count > 0) {
            current_thread->recvany_wait_count = wait_count;
            current_thread->recvany_wait_match_index = RECVANY_NO_MATCH;
            current_thread->recvany_wait_bits = 0;
            current_thread->recvany_wait_active = true;

            for (uint32_t i = 0; i < wait_count; i++) {
                current_thread->recvany_wait_ntfns[i] = wait_ntfns[i];
                current_thread->recvany_wait_nodes[i].prev = NULL;
                current_thread->recvany_wait_nodes[i].next = NULL;
                list_add_tail(&current_thread->recvany_wait_nodes[i], &wait_ntfns[i]->wait_queue.node);
            }

            current_thread->wake_reason = WAKE_NONE;
            current_thread->blocked_endpoint = NULL;
            current_thread->state = BLOCKED;

            if (timeout_ms > 0) {
                tick_t now = get_ticks();
                tick_t ticks = ((uint64_t)timeout_ms * (uint64_t)TICK_HZ) / 1000u;
                if (ticks == 0)
                    ticks = 1;
                current_thread->wake_tick = now + ticks;
                sleep_queue_insert(current_thread);
            } else {
                current_thread->wake_tick = 0;
            }

            schedule();

            if (timeout_ms > 0 && current_thread->wake_reason != WAKE_TIMEOUT &&
                current_thread->timeout_node.prev && current_thread->timeout_node.next) {
                list_remove(&current_thread->timeout_node);
            }

            if (current_thread->wake_reason == WAKE_TIMEOUT) {
                thread_recvany_clear_waits(current_thread);
                continue;
            }

            if ((int32_t)frame->r[0] == ERR_DEAD) {
                thread_recvany_clear_waits(current_thread);
                frame->r[0] = ERR_DEAD;
                return;
            }

            if (current_thread->recvany_wait_match_index == RECVANY_NO_MATCH) {
                thread_recvany_clear_waits(current_thread);
                continue;
            }

            recvany_deliver_notification(current_thread->recvany_wait_match_index,
                                         current_thread->recvany_wait_bits,
                                         &result);
            thread_recvany_clear_waits(current_thread);
            if (!copy_to_user((void *)result_ptr, &result, sizeof(result))) {
                frame->r[0] = ERR_BADPTR;
                return;
            }
            frame->r[0] = 0;
            return;
        }

        if (timeout_ms > 0) {
            tick_t now = get_ticks();
            if (now >= deadline) {
                if (!recvany_write_timeout_result(result_ptr)) {
                    frame->r[0] = ERR_BADPTR;
                    return;
                }
                frame->r[0] = 0;
                return;
            }

            tick_t next_wake = now + 1;
            if (next_wake > deadline)
                next_wake = deadline;
            current_thread->wake_tick = next_wake;
        } else {
            current_thread->wake_tick = get_ticks() + 1;
        }

        current_thread->wake_reason = WAKE_NONE;
        current_thread->state = BLOCKED;
        sleep_queue_insert(current_thread);
        schedule();

        if (wait_count > 0) {
            thread_recvany_clear_waits(current_thread);
        }
    }
}
