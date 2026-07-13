#include "sys_ipc.h"
#include "kernel/syscall/syscall.h"
#include "port.h"
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

#define RECVANY_MAX_HANDLES 16u
#define RECVANY_KIND_SEND 0u
#define RECVANY_KIND_CALL 1u
#define RECVANY_KIND_NTFN 2u
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

static bool trap_frame_sane(const arch_regs_t *tf)
{
    uintptr_t p = (uintptr_t)tf;
    if (p == 0 || (p & 0x3u) != 0)
        return false;

    bool in_stack = false;
    if (kernel_layout.stack_base_va && kernel_layout.stack_top_va &&
        p >= kernel_layout.stack_base_va &&
        p + sizeof(arch_regs_t) <= kernel_layout.stack_top_va)
        in_stack = true;

    if (p >= KSTACK_REGION_BASE && p + sizeof(arch_regs_t) <= KSTACK_REGION_TOP)
        in_stack = true;

    if (!in_stack)
        return false;

    /* Content check: every frame handled by the IPC paths belongs to a user
     * thread blocked in a syscall, so its saved return PC must be a nonzero
     * user VA. A zero/kernel PC here means the frame was clobbered (e.g. by
     * a nested exception) and would resume user mode into a fault. */
    reg_t pc = arch_regs_pc(tf);
    if (pc == 0 || pc >= USER_VA_TOP)
        return false;

    return true;
}

static void ipc_panic_bad_trap_frame(const char *where, const process_t *owner, const arch_regs_t *tf)
{
    KERROR("bad trap_frame at %s: owner_pid=%u tf=%p current_pid=%u", where,
           owner ? owner->pid : 0u,
           tf,
           current_thread && current_thread->owner_process ? current_thread->owner_process->pid : 0u);
    if (tf && ((uintptr_t)tf & 0x3u) == 0)
        KERROR("  frame: pc=%p lr=%p sp=%p cpsr=%p", (void *)arch_regs_pc(tf),
               (void *)arch_regs_lr(tf), (void *)arch_regs_sp(tf),
               (void *)arch_regs_flags(tf));
    panic("Corrupt trap_frame in IPC path");
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

static endpoint_t *validate_endpoint_handle(process_t *proc, handle_t handle, arch_regs_t *frame)
{
    if (!proc)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }

    handle_entry_t *entry = handle_vec_get(&proc->handle_table, handle);
    if (!entry)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }
    if (entry->type != HANDLE_ENDPOINT)
    {
        (*arch_reg(frame, 0)) = ERR_NOPERM;
        return NULL;
    }
    if (!entry->ep)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }
    if (!entry->ep->alive)
    {
        (*arch_reg(frame, 0)) = ERR_DEAD;
        return NULL;
    }

    return entry->ep;
}

static notification_t *validate_notification_handle(process_t *proc, handle_t handle, arch_regs_t *frame)
{
    if (!proc)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }

    handle_entry_t *entry = handle_vec_get(&proc->handle_table, handle);
    if (!entry)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }
    if (entry->type != HANDLE_NOTIFICATION)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }
    if (!entry->ntfn)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }
    if (!entry->ntfn->alive)
    {
        (*arch_reg(frame, 0)) = ERR_DEAD;
        return NULL;
    }

    return entry->ntfn;
}

static handle_entry_t *validate_reply_handle(process_t *proc,
                                             handle_t handle_idx,
                                             thread_t **target_out,
                                             arch_regs_t *frame)
{
    if (!proc || handle_idx == 0)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }

    handle_entry_t *entry = handle_vec_get(&proc->handle_table, handle_idx);
    if (!entry)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }
    if (entry->type != HANDLE_REPLY)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }
    if (!entry->reply || entry->reply->caller_tid == 0)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
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
        (*arch_reg(frame, 0)) = ERR_DEAD;
        return NULL;
    }

    if (target->ipc_state != IPC_WAITING)
    {
        process_untrack_reply_cap(entry->reply);
        kfree_reply_cap(entry->reply);
        entry->reply = NULL;
        entry->grantable = false;
        entry->type = HANDLE_FREE;
        (*arch_reg(frame, 0)) = ERR_DEAD;
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
    result->kind = RECVANY_KIND_NTFN;
    result->source = bits;
    result->r1 = bits;
}

void __attribute__((hot)) sys_msg_send(arch_regs_t *frame)
{
    int handle = (int)(*arch_reg(frame, 0));

    endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!ep)
    {
        return;
    }

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        thread_wait_slot_t *rx_slot = container_of(receiver, thread_wait_slot_t, node);
        thread_t *rx_thread = rx_slot->owner;

        if (rx_thread->recvany_ep_wait_active) {
            recvany_result_t *res = &rx_thread->recvany_pending_result;
            memset(res, 0, sizeof(*res));
            res->matched_index = rx_slot->index;
            res->kind = RECVANY_KIND_SEND;
            res->source = current_thread->owner_process->pid;
            res->r1 = (*arch_reg(frame, 1));
            res->r2 = (*arch_reg(frame, 2));
            res->r3 = (*arch_reg(frame, 3));
            thread_recvany_clear_waits(rx_thread);
            thread_recvany_clear_ep_waits(rx_thread);
            rx_thread->recvany_ep_wait_match_index = rx_slot->index;
            ipc_cancel_timeout(rx_thread);
            rx_thread->wake_reason = WAKE_IPC;
            rx_thread->state = READY;
            sched_add(rx_thread);
        } else {
            arch_regs_t *rx_frame = rx_thread->trap_frame;
            if (!trap_frame_sane(rx_frame))
                ipc_panic_bad_trap_frame("msg_send.rx", rx_thread->owner_process, rx_frame);
            (*arch_reg(rx_frame, 0)) = current_thread->owner_process->pid;
            (*arch_reg(rx_frame, 1)) = (*arch_reg(frame, 1));
            (*arch_reg(rx_frame, 2)) = (*arch_reg(frame, 2));
            (*arch_reg(rx_frame, 3)) = (*arch_reg(frame, 3));
            rx_thread->ipc_state = IPC_NONE;
            rx_thread->blocked_endpoint = NULL;
            ipc_cancel_timeout(rx_thread);
            rx_thread->wake_reason = WAKE_IPC;
            rx_thread->state = READY;
            sched_add(rx_thread);
        }
        (*arch_reg(frame, 0)) = 0;
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

void __attribute__((hot)) sys_msg_recv(arch_regs_t *frame)
{
    int handle = (int)(*arch_reg(frame, 0));
    uint32_t timeout_ms = (*arch_reg(frame, 1)); // TIMEOUT_POLL / TIMEOUT_INFINITE / finite ms

    endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!ep)
    {
        return;
    }

    if (!list_empty(&ep->sender_queue))
    {
        list_node_t *sender = list_pop_front(&ep->sender_queue);
        thread_t *sr_thread = container_of(sender, thread_t, node);
        arch_regs_t *sr_frame = sr_thread->trap_frame;
        if (!trap_frame_sane(sr_frame))
        {
            ipc_panic_bad_trap_frame("msg_recv.sr", sr_thread->owner_process, sr_frame);
        }

        // Copy message to receiver
        (*arch_reg(frame, 0)) = sr_thread->owner_process->pid;
        (*arch_reg(frame, 1)) = (*arch_reg(sr_frame, 1));
        (*arch_reg(frame, 2)) = (*arch_reg(sr_frame, 2));
        (*arch_reg(frame, 3)) = (*arch_reg(sr_frame, 3));

        if (sr_thread->ipc_state == IPC_SENDER)
        {
            // wake the sender, it's done
            (*arch_reg(sr_frame, 0)) = 0;
            sr_thread->ipc_state = IPC_NONE;
            sr_thread->blocked_endpoint = NULL;
            // Cancel timeout if sender had one
            ipc_cancel_timeout(sr_thread);
            sr_thread->wake_reason = WAKE_IPC;
            sr_thread->state = READY;
            if (sr_thread->ipc_buf_xfer_len > 0) {
                ipc_buf_copy(sr_thread, current_thread, sr_thread->ipc_buf_xfer_len);
                (*arch_reg(frame, 1)) = sr_thread->ipc_buf_xfer_len;
                (*arch_reg(frame, 2)) = 0;
                (*arch_reg(frame, 3)) = 0;
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
                (*arch_reg(sr_thread->trap_frame, 0)) = ERR_NOMEM;
                sr_thread->ipc_state = IPC_NONE;
                sr_thread->blocked_endpoint = NULL;
                // Cancel timeout if sender had one
                ipc_cancel_timeout(sr_thread);
                sr_thread->wake_reason = WAKE_IPC;
                sr_thread->state = READY;
                sched_add(sr_thread);
                (*arch_reg(frame, 0)) = ERR_NOMEM;
                return;
            }

            handle_entry_t *rentry = handle_vec_get(&current_thread->owner_process->handle_table, slot);
            rentry->type = HANDLE_REPLY;
            rentry->grantable = false;
            rentry->reply = rc;
            process_track_reply_cap(sr_thread->owner_process, current_thread->owner_process, (uint32_t)slot, rc);

            (*arch_reg(frame, 0)) = slot;
            (*arch_reg(frame, 1)) = sr_thread->owner_process->pid;
            (*arch_reg(frame, 2)) = (*arch_reg(sr_frame, 1));
            (*arch_reg(frame, 3)) = (*arch_reg(sr_frame, 2));
            if (sr_thread->ipc_buf_xfer_len > 0) {
                ipc_buf_copy(sr_thread, current_thread, sr_thread->ipc_buf_xfer_len);
                (*arch_reg(frame, 2)) = sr_thread->ipc_buf_xfer_len;
                (*arch_reg(frame, 3)) = 0;
                sr_thread->ipc_buf_xfer_len = 0;
            }
        }
    }
    else
    {
        if (timeout_ms == TIMEOUT_POLL)
        {
            (*arch_reg(frame, 0)) = ERR_TIMEOUT;
            return;
        }

        current_thread->ep_wait_slot.owner = current_thread;
        current_thread->ep_wait_slot.index = 0;
        current_thread->ep_wait_slot.node.prev = NULL;
        current_thread->ep_wait_slot.node.next = NULL;
        current_thread->ipc_state = IPC_RECEIVER;
        current_thread->blocked_endpoint = ep;
        current_thread->wake_reason = WAKE_NONE;
        list_add_tail(&current_thread->ep_wait_slot.node, &ep->receiver_queue.node);
        current_thread->state = BLOCKED;

        if (timeout_ms != TIMEOUT_INFINITE)
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

        /* Tripwire for the pc=0 user abort seen on the Pi 4: validate our
         * own frame on the way back out of a blocking recv, and catch an
         * impossible timeout wake on an infinite recv. */
        if (!trap_frame_sane(frame))
            ipc_panic_bad_trap_frame("msg_recv.wake", current_thread->owner_process, frame);
        if (timeout_ms == TIMEOUT_INFINITE && current_thread->wake_reason == WAKE_TIMEOUT)
            ipc_panic_bad_trap_frame("msg_recv.wake-timeout-on-infinite",
                                     current_thread->owner_process, frame);

        if (timeout_ms != TIMEOUT_INFINITE && current_thread->wake_reason != WAKE_TIMEOUT &&
            current_thread->timeout_node.prev && current_thread->timeout_node.next)
        {
            list_remove(&current_thread->timeout_node);
        }

        if (current_thread->wake_reason == WAKE_TIMEOUT)
        {
            (*arch_reg(frame, 0)) = ERR_TIMEOUT;
        }
    }
}

void __attribute__((hot)) sys_msg_call(arch_regs_t *frame)
{
    int handle = (int)(*arch_reg(frame, 0));

    endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!ep)
    {
        return;
    }

    reply_cap_t *rc = kalloc_reply_cap();
    if (!rc) {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;  // caller gets clean error, never blocked
    }
    rc->caller_tid = current_thread ? current_thread->tid : 0;

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        thread_wait_slot_t *rx_slot = container_of(receiver, thread_wait_slot_t, node);
        thread_t *rx_thread = rx_slot->owner;
        arch_regs_t *rx_frame = rx_thread->trap_frame;
        if (!trap_frame_sane(rx_frame))
            ipc_panic_bad_trap_frame("msg_call.rx", rx_thread->owner_process, rx_frame);

        int slot = handle_vec_find_free(&rx_thread->owner_process->handle_table);
        if (slot < 0)
        {
            kfree_reply_cap(rc);
            list_add_tail(&rx_slot->node, &ep->receiver_queue.node);
            (*arch_reg(frame, 0)) = ERR_NOMEM;
            return;
        }

        handle_entry_t *rentry = handle_vec_get(&rx_thread->owner_process->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;
        process_track_reply_cap(current_thread->owner_process, rx_thread->owner_process, (uint32_t)slot, rc);

        if (rx_thread->recvany_ep_wait_active) {
            recvany_result_t *res = &rx_thread->recvany_pending_result;
            memset(res, 0, sizeof(*res));
            res->matched_index = rx_slot->index;
            res->kind = RECVANY_KIND_CALL;
            res->source = (uint32_t)slot;
            res->r1 = current_thread->owner_process->pid;
            res->r2 = (*arch_reg(frame, 1));
            res->r3 = (*arch_reg(frame, 2));
            thread_recvany_clear_waits(rx_thread);
            thread_recvany_clear_ep_waits(rx_thread);
            rx_thread->recvany_ep_wait_match_index = rx_slot->index;
            ipc_cancel_timeout(rx_thread);
            rx_thread->wake_reason = WAKE_IPC;
            rx_thread->state = READY;
            sched_add(rx_thread);
        } else {
            (*arch_reg(rx_frame, 0)) = slot;
            (*arch_reg(rx_frame, 1)) = current_thread->owner_process->pid;
            (*arch_reg(rx_frame, 2)) = (*arch_reg(frame, 1));
            (*arch_reg(rx_frame, 3)) = (*arch_reg(frame, 2));
            rx_thread->ipc_state = IPC_NONE;
            rx_thread->blocked_endpoint = NULL;
            ipc_cancel_timeout(rx_thread);
            rx_thread->wake_reason = WAKE_IPC;
            rx_thread->state = READY;
            sched_add(rx_thread);
        }

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

void __attribute__((hot)) sys_msg_reply(arch_regs_t *frame)
{
    handle_t handle_idx = (*arch_reg(frame, 0));
    thread_t *target_thread = NULL;
    handle_entry_t *entry = validate_reply_handle(current_thread->owner_process, handle_idx, &target_thread, frame);
    if (!entry)
    {
        return;
    }

    // Deliver reply into target's saved frame

    arch_regs_t *target_frame = target_thread->trap_frame;
    if (!trap_frame_sane(target_frame))
    {
        ipc_panic_bad_trap_frame("msg_reply.target", target_thread->owner_process, target_frame);
    }
    (*arch_reg(target_frame, 0)) = 0;           // success
    (*arch_reg(target_frame, 1)) = (*arch_reg(frame, 1)); // reply payload
    (*arch_reg(target_frame, 2)) = (*arch_reg(frame, 2));
    (*arch_reg(target_frame, 3)) = (*arch_reg(frame, 3));

    // Wake the caller
    target_thread->ipc_state = IPC_NONE;
    target_thread->blocked_endpoint = NULL;
    // Cancel timeout if target had one
    ipc_cancel_timeout(target_thread);
    target_thread->wake_reason = WAKE_IPC;
    target_thread->state = READY;
    sched_add(target_thread);

    process_untrack_reply_cap(entry->reply);
    kfree_reply_cap(entry->reply);
    entry->reply = NULL;
    entry->grantable = false;
    entry->type = HANDLE_FREE;
    (*arch_reg(frame, 0)) = 0;
}

void sys_msg_lsend(arch_regs_t *frame)
{
    int handle = (int)(*arch_reg(frame, 0));

    endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!ep)
    {
        return;
    }

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        thread_wait_slot_t *rx_slot = container_of(receiver, thread_wait_slot_t, node);
        thread_t *rx_thread = rx_slot->owner;
        uint32_t xlen = (*arch_reg(frame, 1)) > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : (*arch_reg(frame, 1));

        if (rx_thread->recvany_ep_wait_active) {
            recvany_result_t *res = &rx_thread->recvany_pending_result;
            memset(res, 0, sizeof(*res));
            res->matched_index = rx_slot->index;
            res->kind = RECVANY_KIND_SEND;
            res->source = current_thread->owner_process->pid;
            ipc_buf_copy(current_thread, rx_thread, xlen);
            res->r1 = xlen;
            res->r2 = 0;
            res->r3 = 0;
            thread_recvany_clear_waits(rx_thread);
            thread_recvany_clear_ep_waits(rx_thread);
            rx_thread->recvany_ep_wait_match_index = rx_slot->index;
            ipc_cancel_timeout(rx_thread);
            rx_thread->wake_reason = WAKE_IPC;
            rx_thread->state = READY;
            sched_add(rx_thread);
        } else {
            arch_regs_t *rx_frame = rx_thread->trap_frame;
            if (!trap_frame_sane(rx_frame))
                ipc_panic_bad_trap_frame("msg_lsend.rx", rx_thread->owner_process, rx_frame);
            (*arch_reg(rx_frame, 0)) = current_thread->owner_process->pid;
            (*arch_reg(rx_frame, 1)) = xlen;
            (*arch_reg(rx_frame, 2)) = 0;
            (*arch_reg(rx_frame, 3)) = 0;
            ipc_buf_copy(current_thread, rx_thread, xlen);
            rx_thread->ipc_state = IPC_NONE;
            rx_thread->blocked_endpoint = NULL;
            ipc_cancel_timeout(rx_thread);
            rx_thread->wake_reason = WAKE_IPC;
            rx_thread->state = READY;
            sched_add(rx_thread);
        }
        (*arch_reg(frame, 0)) = 0;
    }
    else
    {
        current_thread->ipc_state = IPC_SENDER;
        current_thread->blocked_endpoint = ep;
        list_add_tail(&current_thread->node, &ep->sender_queue.node);
        current_thread->ipc_buf_xfer_len = (*arch_reg(frame, 1)) > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : (*arch_reg(frame, 1));
        current_thread->state = BLOCKED;
        schedule();
    }
}

void sys_msg_lcall(arch_regs_t *frame)
{
    handle_t handle = (handle_t)(*arch_reg(frame, 0));

    endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!ep)
    {
        return;
    }

    reply_cap_t *rc = kalloc_reply_cap();
    if (!rc) {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;  // caller gets clean error, never blocked
    }
    rc->caller_tid = current_thread ? current_thread->tid : 0;

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        thread_wait_slot_t *rx_slot = container_of(receiver, thread_wait_slot_t, node);
        thread_t *rx_thread = rx_slot->owner;
        arch_regs_t *rx_frame = rx_thread->trap_frame;
        if (!trap_frame_sane(rx_frame))
            ipc_panic_bad_trap_frame("msg_lcall.rx", rx_thread->owner_process, rx_frame);

        int slot = handle_vec_find_free(&rx_thread->owner_process->handle_table);
        if (slot < 0)
        {
            kfree_reply_cap(rc);
            list_add_tail(&rx_slot->node, &ep->receiver_queue.node);
            (*arch_reg(frame, 0)) = ERR_NOMEM;
            return;
        }

        handle_entry_t *rentry = handle_vec_get(&rx_thread->owner_process->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;
        process_track_reply_cap(current_thread->owner_process, rx_thread->owner_process, (uint32_t)slot, rc);

        size_t xlen = (*arch_reg(frame, 1)) > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : (*arch_reg(frame, 1));

        if (rx_thread->recvany_ep_wait_active) {
            recvany_result_t *res = &rx_thread->recvany_pending_result;
            memset(res, 0, sizeof(*res));
            res->matched_index = rx_slot->index;
            res->kind = RECVANY_KIND_CALL;
            res->source = (uint32_t)slot;
            res->r1 = current_thread->owner_process->pid;
            ipc_buf_copy(current_thread, rx_thread, xlen);
            res->r2 = xlen;
            res->r3 = 0;
            thread_recvany_clear_waits(rx_thread);
            thread_recvany_clear_ep_waits(rx_thread);
            rx_thread->recvany_ep_wait_match_index = rx_slot->index;
            ipc_cancel_timeout(rx_thread);
            rx_thread->wake_reason = WAKE_IPC;
            rx_thread->state = READY;
            sched_add(rx_thread);
        } else {
            (*arch_reg(rx_frame, 0)) = slot;
            (*arch_reg(rx_frame, 1)) = current_thread->owner_process->pid;
            (*arch_reg(rx_frame, 2)) = xlen;
            (*arch_reg(rx_frame, 3)) = 0;
            ipc_buf_copy(current_thread, rx_thread, xlen);
            rx_thread->ipc_state = IPC_NONE;
            rx_thread->blocked_endpoint = NULL;
            ipc_cancel_timeout(rx_thread);
            rx_thread->wake_reason = WAKE_IPC;
            rx_thread->state = READY;
            sched_add(rx_thread);
        }

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
        current_thread->ipc_buf_xfer_len = (*arch_reg(frame, 1)) > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : (*arch_reg(frame, 1));
        current_thread->state = BLOCKED;
        schedule();
    }
}

void sys_msg_lreply(arch_regs_t *frame)
{
    handle_t handle_idx = (*arch_reg(frame, 0));
    thread_t *target_thread = NULL;
    handle_entry_t *entry = validate_reply_handle(current_thread->owner_process, handle_idx, &target_thread, frame);
    if (!entry)
    {
        return;
    }

    // Deliver reply into target's saved frame

    arch_regs_t *target_frame = target_thread->trap_frame;
    if (!trap_frame_sane(target_frame))
    {
        ipc_panic_bad_trap_frame("msg_lreply.target", target_thread->owner_process, target_frame);
    }
    size_t xlen = (*arch_reg(frame, 1)) > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : (*arch_reg(frame, 1));
    (*arch_reg(target_frame, 0)) = 0;           // success
    (*arch_reg(target_frame, 1)) = xlen;        // reply payload
    (*arch_reg(target_frame, 2)) = 0;
    (*arch_reg(target_frame, 3)) = 0;
    ipc_buf_copy(current_thread, target_thread, xlen);

    // Wake the caller
    target_thread->ipc_state = IPC_NONE;
    target_thread->blocked_endpoint = NULL;
    // Cancel timeout if target had one
    ipc_cancel_timeout(target_thread);
    target_thread->wake_reason = WAKE_IPC;
    target_thread->state = READY;
    sched_add(target_thread);

    process_untrack_reply_cap(entry->reply);
    kfree_reply_cap(entry->reply);
    entry->reply = NULL;
    entry->grantable = false;
    entry->type = HANDLE_FREE;
    (*arch_reg(frame, 0)) = 0;
}

static int recvany_deliver_sender(uint32_t matched_index,
                                  thread_t *receiver,
                                  list_node_t *sender_node,
                                  recvany_result_t *result)
{
    thread_t *sr_thread = container_of(sender_node, thread_t, node);
    arch_regs_t *sr_frame = sr_thread->trap_frame;
    if (!trap_frame_sane(sr_frame))
    {
        ipc_panic_bad_trap_frame("waitany.sr", sr_thread->owner_process, sr_frame);
    }

    memset(result, 0, sizeof(*result));
    result->matched_index = matched_index;

    if (sr_thread->ipc_state == IPC_SENDER)
    {
        result->kind = RECVANY_KIND_SEND;
        result->source = sr_thread->owner_process->pid;
        result->r1 = (*arch_reg(sr_frame, 1));
        result->r2 = (*arch_reg(sr_frame, 2));
        result->r3 = (*arch_reg(sr_frame, 3));

        (*arch_reg(sr_frame, 0)) = 0;
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
            (*arch_reg(sr_frame, 0)) = ERR_NOMEM;
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
        result->r2 = (*arch_reg(sr_frame, 1));
        result->r3 = (*arch_reg(sr_frame, 2));

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
                            uint32_t *wait_ntfn_indices,
                            uint32_t *wait_count_out,
                            endpoint_t **wait_eps,
                            uint32_t *wait_ep_indices,
                            uint32_t *wait_ep_count_out)
{
    endpoint_t *endpoints[RECVANY_MAX_HANDLES];
    notification_t *notifications[RECVANY_MAX_HANDLES];

    if (wait_count_out)
        *wait_count_out = 0;
    if (wait_ep_count_out)
        *wait_ep_count_out = 0;

    for (uint32_t i = 0; i < count; i++) {
        handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handles[i]);
        if (!entry) {
            (*arch_reg(current_thread->trap_frame, 0)) = ERR_BADARG;
            return ERR_BADARG;
        }

        if (entry->type == HANDLE_ENDPOINT) {
            endpoint_t *ep = validate_endpoint_handle(current_thread->owner_process, handles[i], current_thread->trap_frame);
            if (!ep) {
                return (int)(*arch_reg(current_thread->trap_frame, 0));
            }
            endpoints[i] = ep;
            notifications[i] = NULL;
            continue;
        }

        if (entry->type == HANDLE_NOTIFICATION) {
            notification_t *ntfn = validate_notification_handle(current_thread->owner_process, handles[i], current_thread->trap_frame);
            if (!ntfn) {
                return (int)(*arch_reg(current_thread->trap_frame, 0));
            }
            endpoints[i] = NULL;
            notifications[i] = ntfn;
            continue;
        }

        (*arch_reg(current_thread->trap_frame, 0)) = ERR_BADARG;
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
            uint32_t slot = notif_count++;
            if (wait_ntfns)
                wait_ntfns[slot] = notifications[i];
            if (wait_ntfn_indices)
                wait_ntfn_indices[slot] = i;
        }
    }
    if (wait_count_out)
        *wait_count_out = notif_count;

    uint32_t ep_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (endpoints[i]) {
            uint32_t slot = ep_count++;
            if (wait_eps)
                wait_eps[slot] = endpoints[i];
            if (wait_ep_indices)
                wait_ep_indices[slot] = i;
        }
    }
    if (wait_ep_count_out)
        *wait_ep_count_out = ep_count;

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

void sys_waitany(arch_regs_t *frame)
{
    /* r0 = handle array pointer
     * r1 = count
     * r2 = timeout_ms
     * r3 = result struct pointer
     */
    uintptr_t handles_ptr = (uintptr_t)(*arch_reg(frame, 0));
    uint32_t count = (*arch_reg(frame, 1));
    uint32_t timeout_ms = (*arch_reg(frame, 2));
    uintptr_t result_ptr = (uintptr_t)(*arch_reg(frame, 3));

    if (!current_thread || !handles_ptr || !result_ptr ||
        count == 0 || count > RECVANY_MAX_HANDLES) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    if (!validate_user_ptr(result_ptr, sizeof(recvany_result_t)) ||
        !fault_in_pages(current_thread->owner_process->as, result_ptr, sizeof(recvany_result_t), true)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    handle_t handles_local[RECVANY_MAX_HANDLES];
    size_t copy_size = count * sizeof(handle_t);
    if (!copy_from_user(handles_local, (const void *)handles_ptr, copy_size)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    tick_t deadline = 0;
    if (timeout_ms != TIMEOUT_POLL && timeout_ms != TIMEOUT_INFINITE) {
        tick_t ticks = ((uint64_t)timeout_ms * (uint64_t)TICK_HZ) / 1000u;
        if (ticks == 0)
            ticks = 1;
        deadline = get_ticks() + ticks;
    }

    for (;;) {
        notification_t *wait_ntfns[RECVANY_MAX_HANDLES];
        uint32_t wait_ntfn_indices[RECVANY_MAX_HANDLES];
        uint32_t wait_count = 0;
        endpoint_t *wait_eps[RECVANY_MAX_HANDLES];
        uint32_t wait_ep_indices[RECVANY_MAX_HANDLES];
        uint32_t ep_wait_count = 0;
        recvany_result_t result;
        int err = recvany_try_once(handles_local, count, &result,
                                   wait_ntfns, wait_ntfn_indices, &wait_count,
                                   wait_eps, wait_ep_indices, &ep_wait_count);
        if (err == 0) {
            if (!copy_to_user((void *)result_ptr, &result, sizeof(result))) {
                (*arch_reg(frame, 0)) = ERR_BADPTR;
                return;
            }
            (*arch_reg(frame, 0)) = 0;
            return;
        }

        if (err != ERR_BUSY) {
            (*arch_reg(frame, 0)) = err;
            return;
        }

        if (timeout_ms == TIMEOUT_POLL) {
            (*arch_reg(frame, 0)) = ERR_TIMEOUT;
            return;
        }

        /* Deadline check before blocking */
        if (timeout_ms != TIMEOUT_INFINITE) {
            tick_t now = get_ticks();
            if (now >= deadline) {
                if (!recvany_write_timeout_result(result_ptr)) {
                    (*arch_reg(frame, 0)) = ERR_BADPTR;
                    return;
                }
                (*arch_reg(frame, 0)) = 0;
                return;
            }
        }

        /* Enqueue on notification wait queues */
        if (wait_count > 0) {
            current_thread->recvany_wait_count = wait_count;
            current_thread->recvany_wait_match_index = RECVANY_NO_MATCH;
            current_thread->recvany_wait_bits = 0;
            current_thread->recvany_wait_active = true;

            for (uint32_t i = 0; i < wait_count; i++) {
                current_thread->recvany_wait_ntfns[i] = wait_ntfns[i];
                current_thread->recvany_wait_slots[i].owner = current_thread;
                current_thread->recvany_wait_slots[i].index = wait_ntfn_indices[i];
                current_thread->recvany_wait_slots[i].node.prev = NULL;
                current_thread->recvany_wait_slots[i].node.next = NULL;
                list_add_tail(&current_thread->recvany_wait_slots[i].node,
                              &wait_ntfns[i]->wait_queue.node);
            }
        }

        /* Enqueue on endpoint receiver queues */
        if (ep_wait_count > 0) {
            current_thread->recvany_ep_wait_count = ep_wait_count;
            current_thread->recvany_ep_wait_match_index = RECVANY_NO_MATCH;
            current_thread->recvany_ep_wait_active = true;

            for (uint32_t i = 0; i < ep_wait_count; i++) {
                current_thread->recvany_wait_eps[i] = wait_eps[i];
                current_thread->recvany_ep_wait_slots[i].owner = current_thread;
                current_thread->recvany_ep_wait_slots[i].index = wait_ep_indices[i];
                current_thread->recvany_ep_wait_slots[i].node.prev = NULL;
                current_thread->recvany_ep_wait_slots[i].node.next = NULL;
                list_add_tail(&current_thread->recvany_ep_wait_slots[i].node,
                              &wait_eps[i]->receiver_queue.node);
            }
        }

        current_thread->wake_reason = WAKE_NONE;
        current_thread->blocked_endpoint = NULL;
        current_thread->state = BLOCKED;

        if (timeout_ms != TIMEOUT_INFINITE) {
            current_thread->wake_tick = deadline;
            sleep_queue_insert(current_thread);
        } else {
            current_thread->wake_tick = 0;
        }

        schedule();

        /* Cancel sleep queue entry if not timed out */
        if (timeout_ms != TIMEOUT_INFINITE && current_thread->wake_reason != WAKE_TIMEOUT &&
            current_thread->timeout_node.prev && current_thread->timeout_node.next) {
            list_remove(&current_thread->timeout_node);
        }

        /* ERR_DEAD from cap_destroy */
        if ((int32_t)(*arch_reg(frame, 0)) == ERR_DEAD) {
            thread_recvany_clear_waits(current_thread);
            thread_recvany_clear_ep_waits(current_thread);
            (*arch_reg(frame, 0)) = ERR_DEAD;
            return;
        }

        /* Timeout */
        if (current_thread->wake_reason == WAKE_TIMEOUT) {
            thread_recvany_clear_waits(current_thread);
            thread_recvany_clear_ep_waits(current_thread);
            continue; /* deadline check at top catches expiry */
        }

        /* Woken by endpoint sender */
        if (current_thread->recvany_ep_wait_match_index != RECVANY_NO_MATCH) {
            thread_recvany_clear_waits(current_thread);
            thread_recvany_clear_ep_waits(current_thread);
            if (!copy_to_user((void *)result_ptr, &current_thread->recvany_pending_result,
                              sizeof(recvany_result_t))) {
                (*arch_reg(frame, 0)) = ERR_BADPTR;
                return;
            }
            (*arch_reg(frame, 0)) = 0;
            return;
        }

        /* Woken by notification */
        if (current_thread->recvany_wait_match_index != RECVANY_NO_MATCH) {
            recvany_deliver_notification(current_thread->recvany_wait_match_index,
                                         current_thread->recvany_wait_bits,
                                         &result);
            thread_recvany_clear_waits(current_thread);
            thread_recvany_clear_ep_waits(current_thread);
            if (!copy_to_user((void *)result_ptr, &result, sizeof(result))) {
                (*arch_reg(frame, 0)) = ERR_BADPTR;
                return;
            }
            (*arch_reg(frame, 0)) = 0;
            return;
        }

        /* Spurious wakeup — retry */
        thread_recvany_clear_waits(current_thread);
        thread_recvany_clear_ep_waits(current_thread);
    }
}
