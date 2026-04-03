#include "sys_ipc.h"
#include "kernel/syscall/syscall.h"
#include "endpoint.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/kstack.h"
#include "kernel/layout.h"
#include "core/panic.h"
#include <mem.h>
#include <stdbool.h>
#include <zuzu/ipcx.h>

#include "kernel/irq/sys_irq.h"

extern process_t *current_process;

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
    if (!entry->reply || !entry->reply->caller)
    {
        frame->r[0] = ERR_BADARG;
        return NULL;
    }

    process_t *target = entry->reply->caller;
    if (target->ipc_state != IPC_WAITING)
    {
        frame->r[0] = ERR_BADARG;
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

    endpoint_t *ep = validate_endpoint_handle(current_process, handle, frame);
    if (!ep)
    {
        return;
    }

    if (ep->bound_irq >= 0 && irq_check_and_clear_pending(ep->bound_irq))
    {
        frame->r[0] = 0;
        frame->r[1] = ep->bound_irq;
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
            // allocate reply cap

            reply_cap_t *rc = kmalloc(sizeof(reply_cap_t));
            if (!rc)
            {
                frame->r[0] = ERR_NOMEM;
                list_add_tail(&sr_proc->node, &ep->sender_queue.node);
                return;
            }
            rc->caller = sr_proc;

            int slot = handle_vec_find_free(&current_process->handle_table);
            if (slot < 0)
            {
                kfree(rc);
                list_add_tail(&sr_proc->node, &ep->sender_queue.node);
                frame->r[0] = ERR_NOMEM;
                return;
            }

            handle_entry_t *rentry = handle_vec_get(&current_process->handle_table, slot);
            rentry->type = HANDLE_REPLY;
            rentry->grantable = false;
            rentry->reply = rc;

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
        current_process->ipc_state = IPC_RECEIVER;
        current_process->blocked_endpoint = ep;
        list_add_tail(&current_process->node, &ep->receiver_queue.node);
        current_process->process_state = PROCESS_BLOCKED;
        schedule();
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

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        process_t *rx_proc = container_of(receiver, process_t, node);
        exception_frame_t *rx_frame = rx_proc->trap_frame;
        if (!trap_frame_sane(rx_frame))
        {
            ipc_panic_bad_trap_frame("proc_call.rx", rx_proc, rx_frame);
        }
        // allocate reply cap in rx_proc's table
        reply_cap_t *rc = kmalloc(sizeof(reply_cap_t));
        if (!rc)
        {
            frame->r[0] = ERR_NOMEM;
            list_add_tail(&rx_proc->node, &ep->sender_queue.node);
            return;
        }
        rc->caller = current_process;

        int slot = handle_vec_find_free(&rx_proc->handle_table);
        if (slot < 0)
        {
            kfree(rc);
            list_add_tail(&rx_proc->node, &ep->sender_queue.node);
            frame->r[0] = ERR_NOMEM;
            return;
        }

        handle_entry_t *rentry = handle_vec_get(&rx_proc->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;

        rx_frame->r[0] = slot;
        rx_frame->r[1] = current_process->pid;
        rx_frame->r[2] = frame->r[1];
        rx_frame->r[3] = frame->r[2];

        rx_proc->ipc_state = IPC_NONE;
        rx_proc->blocked_endpoint = NULL;
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
    target->process_state = PROCESS_READY;
    sched_add(target);

    kfree(entry->reply);
    // mark the slot free
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

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        process_t *rx_proc = container_of(receiver, process_t, node);
        exception_frame_t *rx_frame = rx_proc->trap_frame;
        if (!trap_frame_sane(rx_frame))
        {
            ipc_panic_bad_trap_frame("proc_callx.rx", rx_proc, rx_frame);
        }
        // allocate reply cap in rx_proc's table
        reply_cap_t *rc = kmalloc(sizeof(reply_cap_t));
        if (!rc)
        {
            frame->r[0] = ERR_NOMEM;
            list_add_tail(&rx_proc->node, &ep->sender_queue.node);
            return;
        }
        rc->caller = current_process;

        int slot = handle_vec_find_free(&rx_proc->handle_table);
        if (slot < 0)
        {
            kfree(rc);
            list_add_tail(&rx_proc->node, &ep->sender_queue.node);
            frame->r[0] = ERR_NOMEM;
            return;
        }

        handle_entry_t *rentry = handle_vec_get(&rx_proc->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;

        rx_frame->r[0] = slot;
        rx_frame->r[1] = current_process->pid;
        rx_frame->r[2] = frame->r[1];
        rx_frame->r[3] = 0;
        ipc_buf_copy(current_process, rx_proc, frame->r[1]);

        rx_proc->ipc_state = IPC_NONE;
        rx_proc->blocked_endpoint = NULL;
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
    target->process_state = PROCESS_READY;
    sched_add(target);

    kfree(entry->reply);
    // mark the slot free
    entry->type = HANDLE_FREE;
    frame->r[0] = 0;
}