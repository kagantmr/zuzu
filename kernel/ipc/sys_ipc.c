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

#include "kernel/irq/sys_irq.h"

extern process_t *current_process;
extern list_head_t sleep_queue;

#define LOG_FMT(fmt) "(ipc) " fmt
#include "core/log.h"

#define KSTACK_REGION_TOP (KSTACK_REGION_BASE + (64u * 0x2000u))

extern kernel_layout_t kernel_layout;

static void ipc_buf_copy(process_t *src, process_t *dst, uint32_t len)
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
           current_process ? current_process->pid : 0u);
    panic("Corrupt trap_frame pointer in IPC path");
}

static endpoint_t *validate_endpoint_handle(process_t *proc, int handle, exception_frame_t *frame)
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

    return entry->ep;
}

static handle_entry_t *validate_reply_handle(process_t *proc,
                                             uint32_t handle_idx,
                                             process_t **target_out,
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
    if (!entry->reply || entry->reply->caller_pid == 0)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }

    process_t *target = process_find_by_pid(entry->reply->caller_pid);

    if (!target || target->process_state == PROCESS_ZOMBIE)
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

void proc_send(exception_frame_t *frame)
{
    int handle = (int)frame->r[0];

    endpoint_t *ep = validate_endpoint_handle(current_process, handle, frame);
    if (!ep)
    {
        return;
    }

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        process_t *rx_proc = container_of(receiver, process_t, node);
        exception_frame_t *rx_frame = rx_proc->trap_frame;
        if (!trap_frame_sane(rx_frame))
        {
            ipc_panic_bad_trap_frame("proc_send.rx", rx_proc, rx_frame);
        }
        // KDEBUG("Sending message from process PID %d to process PID %d", current_process->pid,rx_proc->pid);
        rx_frame->r[0] = current_process->pid;
        rx_frame->r[1] = frame->r[1];
        rx_frame->r[2] = frame->r[2];
        rx_frame->r[3] = frame->r[3];
        rx_proc->ipc_state = IPC_NONE;
        rx_proc->blocked_endpoint = NULL;
        // Cancel timeout if receiver had one
        if (rx_proc->wake_tick != 0 &&
            rx_proc->timeout_node.prev && rx_proc->timeout_node.next) {
            list_remove(&rx_proc->timeout_node);
        }
        rx_proc->wake_tick = 0;
        rx_proc->wake_reason = WAKE_IPC;
        rx_proc->process_state = PROCESS_READY;
        sched_add(rx_proc);
        frame->r[0] = 0;
    }
    else
    {
        current_process->ipc_state = IPC_SENDER;
        current_process->blocked_endpoint = ep;
        list_add_tail(&current_process->node, &ep->sender_queue.node);
        current_process->process_state = PROCESS_BLOCKED;
        schedule();
    }
}

void proc_recv(exception_frame_t *frame)
{
    int handle = (int)frame->r[0];
    uint32_t timeout_ms = frame->r[1]; // 0 = infinite (backward compatible)

    endpoint_t *ep = validate_endpoint_handle(current_process, handle, frame);
    if (!ep)
    {
        return;
    }

    if (!list_empty(&ep->sender_queue))
    {
        // KDEBUG("sender queue NOT empty");
        list_node_t *sender = list_pop_front(&ep->sender_queue);
        process_t *sr_proc = container_of(sender, process_t, node);
        exception_frame_t *sr_frame = (sr_proc->trap_frame);
        if (!trap_frame_sane(sr_frame))
        {
            ipc_panic_bad_trap_frame("proc_recv.sr", sr_proc, sr_frame);
        }

        // Copy message to receiver
        // KDEBUG("Got message from process PID %d as process PID %d", sr_proc->pid,current_process->pid);
        frame->r[0] = sr_proc->pid;
        frame->r[1] = sr_frame->r[1];
        frame->r[2] = sr_frame->r[2];
        frame->r[3] = sr_frame->r[3];

        if (sr_proc->ipc_state == IPC_SENDER)
        {
            // wake the sender, it's done
            sr_frame->r[0] = 0;
            sr_proc->ipc_state = IPC_NONE;
            sr_proc->blocked_endpoint = NULL;
            // Cancel timeout if sender had one
            if (sr_proc->wake_tick != 0 &&
                sr_proc->timeout_node.prev && sr_proc->timeout_node.next) {
                list_remove(&sr_proc->timeout_node);
            }
            sr_proc->wake_tick = 0;
            sr_proc->wake_reason = WAKE_IPC;
            sr_proc->process_state = PROCESS_READY;
            if (sr_proc->ipc_buf_xfer_len > 0) {
                ipc_buf_copy(sr_proc, current_process, sr_proc->ipc_buf_xfer_len);
                frame->r[2] = 0;
                frame->r[3] = 0;
                sr_proc->ipc_buf_xfer_len = 0;
            }
            sched_add(sr_proc);
        }
        else if (sr_proc->ipc_state == IPC_WAITING)
        {
            // Use the pre-allocated reply cap
            reply_cap_t *rc = sr_proc->pending_reply_cap;
            sr_proc->pending_reply_cap = NULL;
            // rc is guaranteed non-NULL — caller pre-allocated it

            int slot = handle_vec_find_free(&current_process->handle_table);
            if (slot < 0) {
                // Handle table full - but at least we can report the error
                // and the caller's rc gets cleaned up
                kfree_reply_cap(rc);
                sr_proc->pending_reply_cap = NULL;
                // Wake the caller with an error instead of leaving it stuck
                sr_proc->trap_frame->r[0] = ERR_NOMEM;
                sr_proc->ipc_state = IPC_NONE;
                sr_proc->blocked_endpoint = NULL;
                // Cancel timeout if sender had one
                if (sr_proc->wake_tick != 0 &&
                    sr_proc->timeout_node.prev && sr_proc->timeout_node.next) {
                    list_remove(&sr_proc->timeout_node);
                }
                sr_proc->wake_tick = 0;
                sr_proc->wake_reason = WAKE_IPC;
                sr_proc->process_state = PROCESS_READY;
                sched_add(sr_proc);
                frame->r[0] = ERR_NOMEM;
                return;
            }

            handle_entry_t *rentry = handle_vec_get(&current_process->handle_table, slot);
            rentry->type = HANDLE_REPLY;
            rentry->grantable = false;
            rentry->reply = rc;
            process_track_reply_cap(sr_proc, current_process, (uint32_t)slot, rc);

            frame->r[0] = slot;
            frame->r[1] = sr_proc->pid;
            frame->r[2] = sr_frame->r[1];
            frame->r[3] = sr_frame->r[2];
            if (sr_proc->ipc_buf_xfer_len > 0) {
                ipc_buf_copy(sr_proc, current_process, sr_proc->ipc_buf_xfer_len);
                frame->r[3] = 0;
                sr_proc->ipc_buf_xfer_len = 0;
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

        current_process->ipc_state = IPC_RECEIVER;
        current_process->blocked_endpoint = ep;
        current_process->wake_reason = WAKE_NONE;
        list_add_tail(&current_process->node, &ep->receiver_queue.node);
        current_process->process_state = PROCESS_BLOCKED;

        if (timeout_ms > 0)
        {
            uint64_t ticks = ((uint64_t)timeout_ms * (uint64_t)TICK_HZ) / 1000u;
            if (ticks == 0)
                ticks = 1;
            current_process->wake_tick = get_ticks() + ticks;
            list_add_tail(&current_process->timeout_node, &sleep_queue.node);
        }
        else
        {
            current_process->wake_tick = 0;
        }

        schedule();

        if (timeout_ms > 0 && current_process->wake_reason != WAKE_TIMEOUT &&
            current_process->timeout_node.prev && current_process->timeout_node.next)
        {
            list_remove(&current_process->timeout_node);
        }

        if (current_process->wake_reason == WAKE_TIMEOUT)
        {
            frame->r[0] = ERR_BUSY;
        }
        // KDEBUG("Listener woke from recv, PID %d", current_process->pid);
    }
}

void proc_call(exception_frame_t *frame)
{
    int handle = (int)frame->r[0];

    endpoint_t *ep = validate_endpoint_handle(current_process, handle, frame);
    if (!ep)
    {
        return;
    }

    reply_cap_t *rc = kalloc_reply_cap();
    if (!rc) {
        frame->r[0] = ERR_NOMEM;
        return;  // caller gets clean error, never blocked
    }
    rc->caller_pid = current_process->pid;

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        process_t *rx_proc = container_of(receiver, process_t, node);
        exception_frame_t *rx_frame = rx_proc->trap_frame;
        if (!trap_frame_sane(rx_frame))
        {
            ipc_panic_bad_trap_frame("proc_call.rx", rx_proc, rx_frame);
        }
        // Use pre-allocated rc from this call
        int slot = handle_vec_find_free(&rx_proc->handle_table);
        if (slot < 0)
        {
            kfree_reply_cap(rc);
            list_add_tail(&rx_proc->node, &ep->sender_queue.node);
            frame->r[0] = ERR_NOMEM;
            return;
        }

        handle_entry_t *rentry = handle_vec_get(&rx_proc->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;
        process_track_reply_cap(current_process, rx_proc, (uint32_t)slot, rc);

        rx_frame->r[0] = slot;
        rx_frame->r[1] = current_process->pid;
        rx_frame->r[2] = frame->r[1];
        rx_frame->r[3] = frame->r[2];

        rx_proc->ipc_state = IPC_NONE;
        rx_proc->blocked_endpoint = NULL;
        // Cancel timeout if receiver had one
        if (rx_proc->wake_tick != 0 &&
            rx_proc->timeout_node.prev && rx_proc->timeout_node.next) {
            list_remove(&rx_proc->timeout_node);
        }
        rx_proc->wake_tick = 0;
        rx_proc->wake_reason = WAKE_IPC;
        rx_proc->process_state = PROCESS_READY;
        sched_add(rx_proc);

        current_process->process_state = PROCESS_BLOCKED;
        current_process->blocked_endpoint = ep;
        current_process->ipc_state = IPC_WAITING;
        schedule();
    }
    else
    {
        current_process->ipc_state = IPC_WAITING;
        current_process->blocked_endpoint = ep;
        current_process->pending_reply_cap = rc;
        list_add_tail(&current_process->node, &ep->sender_queue.node);
        current_process->process_state = PROCESS_BLOCKED;
        schedule();
    }
}

void proc_reply(exception_frame_t *frame)
{
    uint32_t handle_idx = frame->r[0];
    process_t *target = NULL;
    handle_entry_t *entry = validate_reply_handle(current_process, handle_idx, &target, frame);
    if (!entry)
    {
        return;
    }

    // Deliver reply into target's saved frame

    exception_frame_t *target_frame = target->trap_frame;
    if (!trap_frame_sane(target_frame))
    {
        ipc_panic_bad_trap_frame("proc_reply.target", target, target_frame);
    }
    target_frame->r[0] = 0;           // success
    target_frame->r[1] = frame->r[1]; // reply payload
    target_frame->r[2] = frame->r[2];
    target_frame->r[3] = frame->r[3];

    // Wake the caller
    target->ipc_state = IPC_NONE;
    target->blocked_endpoint = NULL;
    // Cancel timeout if target had one
    if (target->wake_tick != 0 &&
        target->timeout_node.prev && target->timeout_node.next) {
        list_remove(&target->timeout_node);
    }
    target->wake_tick = 0;
    target->wake_reason = WAKE_IPC;
    target->process_state = PROCESS_READY;
    sched_add(target);

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

    endpoint_t *ep = validate_endpoint_handle(current_process, handle, frame);
    if (!ep)
    {
        return;
    }

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        process_t *rx_proc = container_of(receiver, process_t, node);
        exception_frame_t *rx_frame = rx_proc->trap_frame;
        if (!trap_frame_sane(rx_frame))
        {
            ipc_panic_bad_trap_frame("proc_sendx.rx", rx_proc, rx_frame);
        }
        // KDEBUG("Sending message from process PID %d to process PID %d", current_process->pid,rx_proc->pid);
        rx_frame->r[0] = current_process->pid;
        rx_frame->r[1] = frame->r[1];
        rx_frame->r[2] = 0;
        rx_frame->r[3] = 0;
        ipc_buf_copy(current_process, rx_proc, frame->r[1]);
        rx_proc->ipc_state = IPC_NONE;
        rx_proc->blocked_endpoint = NULL;
        // Cancel timeout if receiver had one
        if (rx_proc->wake_tick != 0 &&
            rx_proc->timeout_node.prev && rx_proc->timeout_node.next) {
            list_remove(&rx_proc->timeout_node);
        }
        rx_proc->wake_tick = 0;
        rx_proc->wake_reason = WAKE_IPC;
        rx_proc->process_state = PROCESS_READY;
        sched_add(rx_proc);
        frame->r[0] = 0;
    }
    else
    {
        current_process->ipc_state = IPC_SENDER;
        current_process->blocked_endpoint = ep;
        list_add_tail(&current_process->node, &ep->sender_queue.node);
        current_process->ipc_buf_xfer_len = frame->r[1];
        current_process->process_state = PROCESS_BLOCKED;
        schedule();
    }
}

void proc_callx(exception_frame_t *frame)
{
    int handle = (int)frame->r[0];

    endpoint_t *ep = validate_endpoint_handle(current_process, handle, frame);
    if (!ep)
    {
        return;
    }

    reply_cap_t *rc = kalloc_reply_cap();
    if (!rc) {
        frame->r[0] = ERR_NOMEM;
        return;  // caller gets clean error, never blocked
    }
    rc->caller_pid = current_process->pid;

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        process_t *rx_proc = container_of(receiver, process_t, node);
        exception_frame_t *rx_frame = rx_proc->trap_frame;
        if (!trap_frame_sane(rx_frame))
        {
            ipc_panic_bad_trap_frame("proc_callx.rx", rx_proc, rx_frame);
        }
        // Use pre-allocated rc from this call
        int slot = handle_vec_find_free(&rx_proc->handle_table);
        if (slot < 0)
        {
            kfree_reply_cap(rc);
            list_add_tail(&rx_proc->node, &ep->sender_queue.node);
            frame->r[0] = ERR_NOMEM;
            return;
        }

        handle_entry_t *rentry = handle_vec_get(&rx_proc->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;
        process_track_reply_cap(current_process, rx_proc, (uint32_t)slot, rc);

        rx_frame->r[0] = slot;
        rx_frame->r[1] = current_process->pid;
        rx_frame->r[2] = frame->r[1];
        rx_frame->r[3] = 0;
        ipc_buf_copy(current_process, rx_proc, frame->r[1]);

        rx_proc->ipc_state = IPC_NONE;
        rx_proc->blocked_endpoint = NULL;
        // Cancel timeout if receiver had one
        if (rx_proc->wake_tick != 0 &&
            rx_proc->timeout_node.prev && rx_proc->timeout_node.next) {
            list_remove(&rx_proc->timeout_node);
        }
        rx_proc->wake_tick = 0;
        rx_proc->wake_reason = WAKE_IPC;
        rx_proc->process_state = PROCESS_READY;
        sched_add(rx_proc);

        current_process->process_state = PROCESS_BLOCKED;
        current_process->blocked_endpoint = ep;
        current_process->ipc_state = IPC_WAITING;
        schedule();
    }
    else
    {
        current_process->ipc_state = IPC_WAITING;
        current_process->blocked_endpoint = ep;
        current_process->pending_reply_cap = rc;
        list_add_tail(&current_process->node, &ep->sender_queue.node);
        current_process->ipc_buf_xfer_len = frame->r[1];
        current_process->process_state = PROCESS_BLOCKED;
        schedule();
    }
}

void proc_replyx(exception_frame_t *frame)
{
    uint32_t handle_idx = frame->r[0];
    process_t *target = NULL;
    handle_entry_t *entry = validate_reply_handle(current_process, handle_idx, &target, frame);
    if (!entry)
    {
        return;
    }

    // Deliver reply into target's saved frame

    exception_frame_t *target_frame = target->trap_frame;
    if (!trap_frame_sane(target_frame))
    {
        ipc_panic_bad_trap_frame("proc_replyx.target", target, target_frame);
    }
    target_frame->r[0] = 0;           // success
    target_frame->r[1] = frame->r[1]; // reply payload
    target_frame->r[2] = 0;
    target_frame->r[3] = 0;
    ipc_buf_copy(current_process, target, frame->r[1]);

    // Wake the caller
    target->ipc_state = IPC_NONE;
    target->blocked_endpoint = NULL;
    // Cancel timeout if target had one
    if (target->wake_tick != 0 &&
        target->timeout_node.prev && target->timeout_node.next) {
        list_remove(&target->timeout_node);
    }
    target->wake_tick = 0;
    target->wake_reason = WAKE_IPC;
    target->process_state = PROCESS_READY;
    sched_add(target);

    process_untrack_reply_cap(entry->reply);
    kfree_reply_cap(entry->reply);
    entry->reply = NULL;
    entry->grantable = false;
    entry->type = HANDLE_FREE;
    frame->r[0] = 0;
}
