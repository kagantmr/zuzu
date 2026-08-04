#include "sys_msg.h"
#include "kernel/syscall/syscall.h"
#include "port.h"
#include "handle.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/kstack.h"
#include "kernel/layout.h"
#include "kernel/time/tick.h"
#include "core/panic.h"
#include <string.h>
#include <stdbool.h>
#include <zuzu/tls.h>
#include <zuzu/types.h>

#include "kernel/irq/sys_irq.h"

#define LOG_FMT(fmt) "(ipc) " fmt
#include "core/log.h"

#define WAITANY_MAX_HANDLES 16u

extern thread_t *current_thread;
extern ListHead sleep_queue;
extern kernel_layout_t kernel_layout;

static void ipc_buf_copy(thread_t *src, thread_t *dst, uint32_t len)
{
    if (!len || !src->ipc_buf_pa || !dst->ipc_buf_pa)
        return;
    if (len > LMSG_BUF_SIZE)
        return;
    memcpy((void *)PA_TO_VA(dst->ipc_buf_pa),
           (void *)PA_TO_VA(src->ipc_buf_pa), len);
}

#ifdef DEBUG
static bool trap_frame_sane(const CpuState *tf)
{
    uintptr_t p = (uintptr_t)tf;
    if (p == 0 || (p & 0x3u) != 0)
        return false;

    bool in_stack = false;
    if (kernel_layout.stack_base_va && kernel_layout.stack_top_va &&
        p >= kernel_layout.stack_base_va &&
        p + sizeof(CpuState) <= kernel_layout.stack_top_va)
        in_stack = true;

    if (p >= KSTACK_REGION_BASE && p + sizeof(CpuState) <= KSTACK_REGION_TOP)
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

static void ipc_panic_bad_trap_frame(const char *where, const process_t *owner, const CpuState *tf)
{
    if (tf && ((uintptr_t)tf & 0x3u) == 0)
        KERROR("  frame: pc=%p lr=%p sp=%p cpsr=%p", (void *)arch_regs_pc(tf),
               (void *)arch_regs_lr(tf), (void *)arch_regs_sp(tf),
               (void *)arch_regs_flags(tf));
    panic("Corrupt trap_frame in IPC path at %s: owner_pid=%u tf=%p current_pid=%u",
          where, (unsigned)(owner ? owner->pid : 0), (const void *)tf,
          (unsigned)(current_thread && current_thread->owner_process ? current_thread->owner_process->pid : 0));
}
#endif

static void ipc_cancel_timeout(thread_t *t)
{
    if (t->wake_tick != 0 &&
        t->timeout_node.prev && t->timeout_node.next)
    {
        list_remove(&t->timeout_node);
    }
    t->wake_tick = 0;
}

static void ipc_wake_ready(thread_t *t)
{
    t->ipc_state = IPC_NONE;
    t->blocked_port = NULL;
    ipc_cancel_timeout(t);
    t->wake_reason = WAKE_IPC;
    t->state = READY;
    sched_add(t);
}

static HandleEntry *validate_endpoint_handle(process_t *proc, Handle handle, CpuState *frame)
{
    if (!proc)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }

    HandleEntry *entry = handle_vec_get(&proc->handle_table, handle);
    if (!entry)
    {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return NULL;
    }
    if (entry->type != HANDLE_PORT)
    {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return NULL;
    }
    if (!entry->port)
    {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return NULL;
    }
    if (!entry->port->alive)
    {
        (*arch_reg(frame, 0)) = ERR_DEAD;
        return NULL;
    }

    return entry;
}

static HandleEntry *validate_notification_handle(process_t *proc, Handle handle, CpuState *frame)
{
    if (!proc)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return NULL;
    }

    HandleEntry *entry = handle_vec_get(&proc->handle_table, handle);
    if (!entry)
    {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return NULL;
    }
    if (entry->type != HANDLE_NTFN)
    {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return NULL;
    }
    if (!entry->ntfn)
    {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return NULL;
    }
    if (!entry->ntfn->alive)
    {
        (*arch_reg(frame, 0)) = ERR_DEAD;
        return NULL;
    }

    return entry;
}

static HandleEntry *validate_reply_handle(process_t *proc,
                                          Handle handle_idx,
                                          thread_t **target_out,
                                          CpuState *frame)
{
    if (!proc || handle_idx == 0)
    {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return NULL;
    }

    HandleEntry *entry = handle_vec_get(&proc->handle_table, handle_idx);
    if (!entry)
    {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return NULL;
    }
    if (entry->type != HANDLE_REPLY)
    {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return NULL;
    }
    if (!entry->reply || entry->reply->caller_tid == 0)
    {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
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

static void waitany_deliver_notification(uint32_t matched_index,
                                         uint32_t bits,
                                         WaitanyResult *result)
{
    memset(result, 0, sizeof(*result));
    result->size = sizeof(*result);
    result->matched_index = matched_index;
    result->kind = WAITANY_KIND_NTFN;
    result->source = 0;
    result->w1 = bits;
}

void __attribute__((hot)) SysMsgSend(CpuState *frame)
{
    int handle = (int)(*arch_reg(frame, 0));

    HandleEntry *entry = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!entry)
        return;
    Port *port = entry->port;
    if (!port)
    {
        return;
    }

    if (!list_empty(&port->receiver_queue))
    {
        ListNode *receiver = list_pop_front(&port->receiver_queue);
        thread_wait_slot_t *rx_slot = container_of(receiver, thread_wait_slot_t, node);
        thread_t *rx_thread = rx_slot->owner;

        if (rx_thread->waitany_ep_wait_active)
        {
            WaitanyResult *res = &rx_thread->waitany_pending_result;
            memset(res, 0, sizeof(*res));
            res->matched_index = rx_slot->index;
            res->kind = WAITANY_KIND_SEND;
            res->source = current_thread->owner_process->pid;
            res->marker = entry->marker;
            res->w1 = (*arch_reg(frame, 1));
            res->w2 = (*arch_reg(frame, 2));
            res->w3 = (*arch_reg(frame, 3));
            res->size = sizeof(*res);
            thread_waitany_clear_waits(rx_thread);
            thread_waitany_clear_ep_waits(rx_thread);
            rx_thread->waitany_ep_wait_match_index = rx_slot->index;
            ipc_cancel_timeout(rx_thread);
            rx_thread->wake_reason = WAKE_IPC;
            rx_thread->state = READY;
            sched_add(rx_thread);
        }
        else
        {
            CpuState *rx_frame = rx_thread->trap_frame;
#ifdef DEBUG
            if (!trap_frame_sane(rx_frame))
                ipc_panic_bad_trap_frame("ZuzuMsgSend.rx", rx_thread->owner_process, rx_frame);
#endif
            (*arch_reg(rx_frame, 0)) = current_thread->owner_process->pid;
            (*arch_reg(rx_frame, 1)) = (*arch_reg(frame, 1));
            (*arch_reg(rx_frame, 2)) = (*arch_reg(frame, 2));
            (*arch_reg(rx_frame, 3)) = (*arch_reg(frame, 3));
            rx_thread->ipc_state = IPC_NONE;
            rx_thread->blocked_port = NULL;
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
        current_thread->blocked_port = port;
        current_thread->ipc_marker = entry->marker;
        list_add_tail(&current_thread->node, &port->sender_queue.node);
        current_thread->state = BLOCKED;
        schedule();
    }
}

void __attribute__((hot)) SysMsgRecv(CpuState *frame)
{
    int handle = (int)(*arch_reg(frame, 0));
    uint32_t timeout_ms = (*arch_reg(frame, 1)); // TIMEOUT_POLL / TIMEOUT_INFINITE / finite ms

    HandleEntry *entry = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!entry)
        return;
    Port *port = entry->port;
    if (!port)
    {
        return;
    }

    if (!list_empty(&port->sender_queue))
    {
        ListNode *sender = list_pop_front(&port->sender_queue);
        thread_t *sr_thread = container_of(sender, thread_t, node);
        CpuState *sr_frame = sr_thread->trap_frame;
#ifdef DEBUG
        if (!trap_frame_sane(sr_frame))
        {
            ipc_panic_bad_trap_frame("ZuzuMsgRecv.sr", sr_thread->owner_process, sr_frame);
        }
#endif
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
            sr_thread->blocked_port = NULL;
            // Cancel timeout if sender had one
            ipc_cancel_timeout(sr_thread);
            sr_thread->wake_reason = WAKE_IPC;
            sr_thread->state = READY;
            if (sr_thread->ipc_buf_xfer_len > 0)
            {
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
            ReplyCap *rc = sr_thread->pending_reply_cap;
            sr_thread->pending_reply_cap = NULL;
            // rc is guaranteed non-NULL — caller pre-allocated it

            int slot = handle_vec_find_free(&current_thread->owner_process->handle_table);
            if (slot < 0)
            {
                // Handle table full - but at least we can report the error
                // and the caller's rc gets cleaned up
                kfree_reply_cap(rc);
                sr_thread->pending_reply_cap = NULL;
                // Wake the caller with an error instead of leaving it stuck
                (*arch_reg(sr_thread->trap_frame, 0)) = ERR_NOMEM;
                sr_thread->ipc_state = IPC_NONE;
                sr_thread->blocked_port = NULL;
                // Cancel timeout if sender had one
                ipc_cancel_timeout(sr_thread);
                sr_thread->wake_reason = WAKE_IPC;
                sr_thread->state = READY;
                sched_add(sr_thread);
                (*arch_reg(frame, 0)) = ERR_NOMEM;
                return;
            }

            HandleEntry *rentry = handle_vec_get(&current_thread->owner_process->handle_table, slot);
            rentry->type = HANDLE_REPLY;
            rentry->grantable = false;
            rentry->reply = rc;
            process_track_reply_cap(sr_thread->owner_process, current_thread->owner_process, (uint32_t)slot, rc);

            (*arch_reg(frame, 0)) = slot;
            (*arch_reg(frame, 1)) = sr_thread->owner_process->pid;
            (*arch_reg(frame, 2)) = (*arch_reg(sr_frame, 1));
            (*arch_reg(frame, 3)) = (*arch_reg(sr_frame, 2));
            if (sr_thread->ipc_buf_xfer_len > 0)
            {
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
        current_thread->blocked_port = port;
        current_thread->wake_reason = WAKE_NONE;
        list_add_tail(&current_thread->ep_wait_slot.node, &port->receiver_queue.node);
        current_thread->state = BLOCKED;

        if (timeout_ms != TIMEOUT_INFINITE)
        {
            Tick ticks = ((uint64_t)timeout_ms * (uint64_t)TICK_HZ) / 1000u;
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

#ifdef DEBUG
        if (!trap_frame_sane(frame))
            ipc_panic_bad_trap_frame("ZuzuMsgRecv.wake", current_thread->owner_process, frame);
        if (timeout_ms == TIMEOUT_INFINITE && current_thread->wake_reason == WAKE_TIMEOUT)
            ipc_panic_bad_trap_frame("ZuzuMsgRecv.wake-timeout-on-infinite",
                                     current_thread->owner_process, frame);
#endif
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

void __attribute__((hot)) SysMsgCall(CpuState *frame)
{
    int handle = (int)(*arch_reg(frame, 0));

    HandleEntry *entry = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!entry)
        return;
    Port *port = entry->port;
    if (!port)
    {
        return;
    }

    ReplyCap *rc = kalloc_reply_cap();
    if (!rc)
    {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return; // caller gets clean error, never blocked
    }
    rc->caller_tid = current_thread ? current_thread->tid : 0;

    if (!list_empty(&port->receiver_queue))
    {
        ListNode *receiver = list_pop_front(&port->receiver_queue);
        thread_wait_slot_t *rx_slot = container_of(receiver, thread_wait_slot_t, node);
        thread_t *rx_thread = rx_slot->owner;
        CpuState *rx_frame = rx_thread->trap_frame;
#ifdef DEBUG
        if (!trap_frame_sane(rx_frame))
            ipc_panic_bad_trap_frame("ZuzuMsgCall.rx", rx_thread->owner_process, rx_frame);
#endif
        int slot = handle_vec_find_free(&rx_thread->owner_process->handle_table);
        if (slot < 0)
        {
            kfree_reply_cap(rc);
            list_add_tail(&rx_slot->node, &port->receiver_queue.node);
            (*arch_reg(frame, 0)) = ERR_NOMEM;
            return;
        }

        HandleEntry *rentry = handle_vec_get(&rx_thread->owner_process->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;
        process_track_reply_cap(current_thread->owner_process, rx_thread->owner_process, (uint32_t)slot, rc);

        if (rx_thread->waitany_ep_wait_active)
        {
            WaitanyResult *res = &rx_thread->waitany_pending_result;
            memset(res, 0, sizeof(*res));
            res->size = sizeof(*res);
            res->matched_index = rx_slot->index;
            res->kind = WAITANY_KIND_CALL;
            res->source = (uint32_t)slot;
            res->marker = entry->marker;
            res->w1 = current_thread->owner_process->pid;
            res->w2 = (*arch_reg(frame, 1));
            res->w3 = (*arch_reg(frame, 2));
            thread_waitany_clear_waits(rx_thread);
            thread_waitany_clear_ep_waits(rx_thread);
            rx_thread->waitany_ep_wait_match_index = rx_slot->index;
        }
        else
        {
            (*arch_reg(rx_frame, 0)) = slot;
            (*arch_reg(rx_frame, 1)) = current_thread->owner_process->pid;
            (*arch_reg(rx_frame, 2)) = (*arch_reg(frame, 1));
            (*arch_reg(rx_frame, 3)) = (*arch_reg(frame, 2));
            rx_thread->ipc_state = IPC_NONE;
            rx_thread->blocked_port = NULL;
        }
        ipc_cancel_timeout(rx_thread);
        rx_thread->wake_reason = WAKE_IPC;

        current_thread->state = BLOCKED;
        current_thread->blocked_port = port;
        current_thread->ipc_state = IPC_WAITING;

        /* Direct handoff: skip the run-queue round trip and switch straight
         * to the receiver we just woke, as long as doing so wouldn't jump
         * ahead of a thread that's already waiting at rx_thread's priority
         * or higher (sched_has_ready_at_or_above) -- in that case the full
         * scheduler wouldn't have picked rx_thread next anyway, so fall back
         * to the normal sched_add()+schedule() path. */
        if (sched_has_ready_at_or_above(rx_thread))
        {
            rx_thread->state = READY;
            sched_add(rx_thread);
            schedule();
        }
        else
        {
            switch_to_thread(rx_thread);
        }
    }
    else
    {
        current_thread->ipc_state = IPC_WAITING;
        current_thread->blocked_port = port;
        current_thread->pending_reply_cap = rc;
        current_thread->ipc_marker = entry->marker;
        list_add_tail(&current_thread->node, &port->sender_queue.node);
        current_thread->state = BLOCKED;
        schedule();
    }
}

void __attribute__((hot)) SysMsgReply(CpuState *frame)
{
    Handle handle_idx = (*arch_reg(frame, 0));
    thread_t *target_thread = NULL;
    HandleEntry *entry = validate_reply_handle(current_thread->owner_process, handle_idx, &target_thread, frame);
    if (!entry)
    {
        return;
    }

    // Deliver reply into target's saved frame

    CpuState *target_frame = target_thread->trap_frame;
#ifdef DEBUG
    if (!trap_frame_sane(target_frame))
    {
        ipc_panic_bad_trap_frame("ZuzuMsgReply.target", target_thread->owner_process, target_frame);
    }
#endif
    (*arch_reg(target_frame, 0)) = 0;                     // success
    (*arch_reg(target_frame, 1)) = (*arch_reg(frame, 1)); // reply payload
    (*arch_reg(target_frame, 2)) = (*arch_reg(frame, 2));
    (*arch_reg(target_frame, 3)) = (*arch_reg(frame, 3));

    // Wake the caller
    target_thread->ipc_state = IPC_NONE;
    target_thread->blocked_port = NULL;
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

void SysMsgLsend(CpuState *frame)
{
    int handle = (int)(*arch_reg(frame, 0));
    uint32_t xlen = (*arch_reg(frame, 1));

    HandleEntry *entry = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!entry)
        return;
    Port *port = entry->port;
    if (!port)
    {
        return;
    }

    /* No truncation: oversized payloads are rejected outright. */
    if (xlen > LMSG_BUF_SIZE)
    {
        (*arch_reg(frame, 0)) = ERR_OVERFLOW;
        return;
    }

    if (!list_empty(&port->receiver_queue))
    {
        ListNode *receiver = list_pop_front(&port->receiver_queue);
        thread_wait_slot_t *rx_slot = container_of(receiver, thread_wait_slot_t, node);
        thread_t *rx_thread = rx_slot->owner;

        if (rx_thread->waitany_ep_wait_active)
        {
            WaitanyResult *res = &rx_thread->waitany_pending_result;
            memset(res, 0, sizeof(*res));
            res->size = sizeof(*res);
            res->matched_index = rx_slot->index;
            res->kind = WAITANY_KIND_SEND;
            res->source = current_thread->owner_process->pid;
            res->marker = entry->marker;
            ipc_buf_copy(current_thread, rx_thread, xlen);
            res->w1 = xlen;
            res->w2 = 0;
            res->w3 = 0;
            thread_waitany_clear_waits(rx_thread);
            thread_waitany_clear_ep_waits(rx_thread);
            rx_thread->waitany_ep_wait_match_index = rx_slot->index;
            ipc_cancel_timeout(rx_thread);
            rx_thread->wake_reason = WAKE_IPC;
            rx_thread->state = READY;
            sched_add(rx_thread);
        }
        else
        {
            CpuState *rx_frame = rx_thread->trap_frame;
#ifdef DEBUG
            if (!trap_frame_sane(rx_frame))
                ipc_panic_bad_trap_frame("ZuzuMsgLsend.rx", rx_thread->owner_process, rx_frame);
#endif
            (*arch_reg(rx_frame, 0)) = current_thread->owner_process->pid;
            (*arch_reg(rx_frame, 1)) = xlen;
            (*arch_reg(rx_frame, 2)) = 0;
            (*arch_reg(rx_frame, 3)) = 0;
            ipc_buf_copy(current_thread, rx_thread, xlen);
            rx_thread->ipc_state = IPC_NONE;
            rx_thread->blocked_port = NULL;
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
        current_thread->blocked_port = port;
        current_thread->ipc_marker = entry->marker;
        list_add_tail(&current_thread->node, &port->sender_queue.node);
        current_thread->ipc_buf_xfer_len = xlen;
        current_thread->state = BLOCKED;
        schedule();
    }
}

void SysMsgLcall(CpuState *frame)
{
    Handle handle = (Handle)(*arch_reg(frame, 0));
    uint32_t xlen = (*arch_reg(frame, 1));

    HandleEntry *entry = validate_endpoint_handle(current_thread->owner_process, handle, frame);
    if (!entry)
        return;
    Port *port = entry->port;
    if (!port)
    {
        return;
    }

    /* No truncation: oversized payloads are rejected outright. */
    if (xlen > LMSG_BUF_SIZE)
    {
        (*arch_reg(frame, 0)) = ERR_OVERFLOW;
        return;
    }

    ReplyCap *rc = kalloc_reply_cap();
    if (!rc)
    {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return; // caller gets clean error, never blocked
    }
    rc->caller_tid = current_thread ? current_thread->tid : 0;

    if (!list_empty(&port->receiver_queue))
    {
        ListNode *receiver = list_pop_front(&port->receiver_queue);
        thread_wait_slot_t *rx_slot = container_of(receiver, thread_wait_slot_t, node);
        thread_t *rx_thread = rx_slot->owner;
        CpuState *rx_frame = rx_thread->trap_frame;
        (void)rx_frame;
#ifdef DEBUG
        if (!trap_frame_sane(rx_frame))
            ipc_panic_bad_trap_frame("ZuzuMsgLcall.rx", rx_thread->owner_process, rx_frame);
#endif
        int slot = handle_vec_find_free(&rx_thread->owner_process->handle_table);
        if (slot < 0)
        {
            kfree_reply_cap(rc);
            list_add_tail(&rx_slot->node, &port->receiver_queue.node);
            (*arch_reg(frame, 0)) = ERR_NOMEM;
            return;
        }

        HandleEntry *rentry = handle_vec_get(&rx_thread->owner_process->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;
        process_track_reply_cap(current_thread->owner_process, rx_thread->owner_process, (uint32_t)slot, rc);

        if (rx_thread->waitany_ep_wait_active)
        {
            WaitanyResult *res = &rx_thread->waitany_pending_result;
            memset(res, 0, sizeof(*res));
            res->size = sizeof(*res);
            res->matched_index = rx_slot->index;
            res->kind = WAITANY_KIND_CALL;
            res->source = (uint32_t)slot;
            res->marker = entry->marker;
            res->w1 = current_thread->owner_process->pid;
            ipc_buf_copy(current_thread, rx_thread, xlen);
            res->w2 = xlen;
            res->w3 = 0;
            thread_waitany_clear_waits(rx_thread);
            thread_waitany_clear_ep_waits(rx_thread);
            rx_thread->waitany_ep_wait_match_index = rx_slot->index;
        }
        else
        {
            (*arch_reg(rx_frame, 0)) = slot;
            (*arch_reg(rx_frame, 1)) = current_thread->owner_process->pid;
            (*arch_reg(rx_frame, 2)) = xlen;
            (*arch_reg(rx_frame, 3)) = 0;
            ipc_buf_copy(current_thread, rx_thread, xlen);
            rx_thread->ipc_state = IPC_NONE;
            rx_thread->blocked_port = NULL;
        }
        ipc_cancel_timeout(rx_thread);
        rx_thread->wake_reason = WAKE_IPC;

        current_thread->state = BLOCKED;
        current_thread->blocked_port = port;
        current_thread->ipc_state = IPC_WAITING;

        /* Direct handoff -- see the identical comment in SysMsgCall(). */
        if (sched_has_ready_at_or_above(rx_thread))
        {
            rx_thread->state = READY;
            sched_add(rx_thread);
            schedule();
        }
        else
        {
            switch_to_thread(rx_thread);
        }
    }
    else
    {
        current_thread->ipc_state = IPC_WAITING;
        current_thread->blocked_port = port;
        current_thread->pending_reply_cap = rc;
        current_thread->ipc_marker = entry->marker;
        list_add_tail(&current_thread->node, &port->sender_queue.node);
        current_thread->ipc_buf_xfer_len = xlen;
        current_thread->state = BLOCKED;
        schedule();
    }
}

void SysMsgLreply(CpuState *frame)
{
    Handle handle_idx = (*arch_reg(frame, 0));
    uint32_t xlen = (*arch_reg(frame, 1));

    /* No truncation: oversized payloads are rejected outright. */
    if (xlen > LMSG_BUF_SIZE)
    {
        (*arch_reg(frame, 0)) = ERR_OVERFLOW;
        return;
    }

    thread_t *target_thread = NULL;
    HandleEntry *entry = validate_reply_handle(current_thread->owner_process, handle_idx, &target_thread, frame);
    if (!entry)
    {
        return;
    }

    // Deliver reply into target's saved frame

    CpuState *target_frame = target_thread->trap_frame;
#ifdef DEBUG
    if (!trap_frame_sane(target_frame))
    {
        ipc_panic_bad_trap_frame("ZuzuMsgLreply.target", target_thread->owner_process, target_frame);
    }
#endif
    (*arch_reg(target_frame, 0)) = 0;    // success
    (*arch_reg(target_frame, 1)) = xlen; // reply payload
    (*arch_reg(target_frame, 2)) = 0;
    (*arch_reg(target_frame, 3)) = 0;
    ipc_buf_copy(current_thread, target_thread, xlen);

    // Wake the caller
    target_thread->ipc_state = IPC_NONE;
    target_thread->blocked_port = NULL;
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

static int waitany_deliver_sender(uint32_t matched_index,
                                  thread_t *receiver,
                                  ListNode *sender_node,
                                  WaitanyResult *result)
{
    thread_t *sr_thread = container_of(sender_node, thread_t, node);
    CpuState *sr_frame = sr_thread->trap_frame;
#ifdef DEBUG
    if (!trap_frame_sane(sr_frame))
    {
        ipc_panic_bad_trap_frame("waitany.sr", sr_thread->owner_process, sr_frame);
    }
#endif
    memset(result, 0, sizeof(*result));
    result->size = sizeof(*result);
    result->matched_index = matched_index;

    if (sr_thread->ipc_state == IPC_SENDER)
    {
        result->kind = WAITANY_KIND_SEND;
        result->source = sr_thread->owner_process->pid;
        result->marker = sr_thread->ipc_marker;
        result->w1 = (*arch_reg(sr_frame, 1));
        result->w2 = (*arch_reg(sr_frame, 2));
        result->w3 = (*arch_reg(sr_frame, 3));

        (*arch_reg(sr_frame, 0)) = 0;
        sr_thread->ipc_state = IPC_NONE;
        sr_thread->blocked_port = NULL;
        ipc_cancel_timeout(sr_thread);
        sr_thread->wake_reason = WAKE_IPC;
        sr_thread->state = READY;

        if (sr_thread->ipc_buf_xfer_len > 0)
        {
            ipc_buf_copy(sr_thread, receiver, sr_thread->ipc_buf_xfer_len);
            result->w1 = sr_thread->ipc_buf_xfer_len;
            result->w2 = 0;
            result->w3 = 0;
            sr_thread->ipc_buf_xfer_len = 0;
        }

        sched_add(sr_thread);
        return 0;
    }

    if (sr_thread->ipc_state == IPC_WAITING)
    {
        ReplyCap *rc = sr_thread->pending_reply_cap;
        sr_thread->pending_reply_cap = NULL;

        int slot = handle_vec_find_free(&receiver->owner_process->handle_table);
        if (slot < 0)
        {
            kfree_reply_cap(rc);
            (*arch_reg(sr_frame, 0)) = ERR_NOMEM;
            ipc_wake_ready(sr_thread);
            return ERR_NOMEM;
        }

        HandleEntry *rentry = handle_vec_get(&receiver->owner_process->handle_table, slot);
        rentry->type = HANDLE_REPLY;
        rentry->grantable = false;
        rentry->reply = rc;
        process_track_reply_cap(sr_thread->owner_process, receiver->owner_process, (uint32_t)slot, rc);

        result->kind = WAITANY_KIND_CALL;
        result->source = (uint32_t)slot;
        result->marker = sr_thread->ipc_marker;
        result->w1 = sr_thread->owner_process->pid;
        result->w2 = (*arch_reg(sr_frame, 1));
        result->w3 = (*arch_reg(sr_frame, 2));

        if (sr_thread->ipc_buf_xfer_len > 0)
        {
            ipc_buf_copy(sr_thread, receiver, sr_thread->ipc_buf_xfer_len);
            result->w2 = sr_thread->ipc_buf_xfer_len;
            result->w3 = 0;
            sr_thread->ipc_buf_xfer_len = 0;
        }

        return 0;
    }

    return ERR_BADARG;
}

static int waitany_try_once(const Handle *handles,
                            uint32_t count,
                            WaitanyResult *result,
                            Ntfn **wait_ntfns,
                            uint32_t *wait_ntfn_indices,
                            uint32_t *wait_count_out,
                            Port **wait_eps,
                            uint32_t *wait_ep_indices,
                            uint32_t *wait_ep_count_out)
{
    Port *endpoints[WAITANY_MAX_HANDLES];
    Ntfn *notifications[WAITANY_MAX_HANDLES];

    if (wait_count_out)
        *wait_count_out = 0;
    if (wait_ep_count_out)
        *wait_ep_count_out = 0;

    for (uint32_t i = 0; i < count; i++)
    {
        HandleEntry *entry = handle_vec_get(&current_thread->owner_process->handle_table, handles[i]);
        if (!entry)
        {
            (*arch_reg(current_thread->trap_frame, 0)) = ERR_BADHANDLE;
            return ERR_BADHANDLE;
        }

        if (entry->type == HANDLE_PORT)
        {
            HandleEntry *ep_entry = validate_endpoint_handle(current_thread->owner_process, handles[i], current_thread->trap_frame);
            if (!ep_entry)
                return (int)(*arch_reg(current_thread->trap_frame, 0));
            Port *port = ep_entry->port;
            if (!port)
            {
                return (int)(*arch_reg(current_thread->trap_frame, 0));
            }
            endpoints[i] = port;
            notifications[i] = NULL;
            continue;
        }

        if (entry->type == HANDLE_NTFN)
        {
            HandleEntry *n_entry = validate_notification_handle(current_thread->owner_process, handles[i], current_thread->trap_frame);
            if (!n_entry)
            {
                return (int)(*arch_reg(current_thread->trap_frame, 0));
            }
            Ntfn *ntfn = n_entry->ntfn;
            if (!ntfn)
            {
                return (int)(*arch_reg(current_thread->trap_frame, 0));
            }
            endpoints[i] = NULL;
            notifications[i] = ntfn;
            continue;
        }

        (*arch_reg(current_thread->trap_frame, 0)) = ERR_BADTYPE;
        return ERR_BADTYPE;
    }

    for (uint32_t i = 0; i < count; i++)
    {
        if (endpoints[i] && !list_empty(&endpoints[i]->sender_queue))
        {
            ListNode *sender = list_pop_front(&endpoints[i]->sender_queue);
            return waitany_deliver_sender(i, current_thread, sender, result);
        }
    }

    for (uint32_t i = 0; i < count; i++)
    {
        Ntfn *ntfn = notifications[i];
        if (ntfn && ntfn->word != 0)
        {
            uint32_t bits = ntfn->word;
            ntfn->word = 0;
            waitany_deliver_notification(i, bits, result);
            return 0;
        }
    }

    uint32_t notif_count = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        if (notifications[i])
        {
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
    for (uint32_t i = 0; i < count; i++)
    {
        if (endpoints[i])
        {
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

static bool waitany_write_timeout_result(uintptr_t result_ptr, uint32_t size)
{
    WaitanyResult result;
    memset(&result, 0, sizeof(result));
    result.size = sizeof(result);
    result.matched_index = UINT32_MAX;
    result.kind = WAITANY_KIND_TIMEOUT;
    return CopyToUser((void *)result_ptr, &result, size);
}

void SysWaitAny(CpuState *frame)
{
    /* r0 = handle array pointer
     * w1 = count
     * w2 = timeout_ms
     * w3 = result struct pointer
     */
    VirtAddr handles_ptr = (VirtAddr)(*arch_reg(frame, 0));
    size_t count         = (*arch_reg(frame, 1));
    Duration timeout_ms  = (*arch_reg(frame, 2));
    VirtAddr result_ptr  = (VirtAddr)(*arch_reg(frame, 3));

    if (!current_thread || !handles_ptr || !result_ptr ||
        count == 0 || count > WAITANY_MAX_HANDLES)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    if (!validate_user_ptr(result_ptr, sizeof(WaitanyResult)) ||
        !fault_in_pages(current_thread->owner_process->as, result_ptr, sizeof(WaitanyResult), true))
    {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    size_t caller_size;
    if (!CopyFromUser(&caller_size, (const void *)result_ptr, sizeof(uint32_t)))
    {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    if (caller_size < sizeof(WaitanyResult))
    { /* v1: exact; later: >= v1 size */
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    size_t wlen = caller_size < sizeof(WaitanyResult)
                      ? caller_size
                      : sizeof(WaitanyResult);

    Handle handles_local[WAITANY_MAX_HANDLES];
    size_t copy_size = count * sizeof(Handle);
    if (!CopyFromUser(handles_local, (const void *)handles_ptr, copy_size))
    {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    Tick deadline = 0;
    if (timeout_ms != TIMEOUT_POLL && timeout_ms != TIMEOUT_INFINITE)
    {
        Tick ticks = ((uint64_t)timeout_ms * (uint64_t)TICK_HZ) / 1000u;
        if (ticks == 0)
            ticks = 1;
        deadline = get_ticks() + ticks;
    }

    for (;;)
    {
        Ntfn *wait_ntfns[WAITANY_MAX_HANDLES];
        uint32_t wait_ntfn_indices[WAITANY_MAX_HANDLES];
        uint32_t wait_count = 0;
        Port *wait_eps[WAITANY_MAX_HANDLES];
        uint32_t wait_ep_indices[WAITANY_MAX_HANDLES];
        uint32_t ep_wait_count = 0;
        WaitanyResult result;
        int err = waitany_try_once(handles_local, count, &result,
                                   wait_ntfns, wait_ntfn_indices, &wait_count,
                                   wait_eps, wait_ep_indices, &ep_wait_count);
        if (err == 0)
        {
            if (!CopyToUser((void *)result_ptr, &result, wlen))
            {
                (*arch_reg(frame, 0)) = ERR_BADPTR;
                return;
            }
            (*arch_reg(frame, 0)) = 0;
            return;
        }

        if (err != ERR_BUSY)
        {
            (*arch_reg(frame, 0)) = err;
            return;
        }

        if (timeout_ms == TIMEOUT_POLL)
        {
            (*arch_reg(frame, 0)) = ERR_TIMEOUT;
            return;
        }

        /* Deadline check before blocking */
        if (timeout_ms != TIMEOUT_INFINITE)
        {
            Tick now = get_ticks();
            if (now >= deadline)
            {
                if (!waitany_write_timeout_result(result_ptr, wlen))
                {
                    (*arch_reg(frame, 0)) = ERR_BADPTR;
                    return;
                }
                (*arch_reg(frame, 0)) = 0;
                return;
            }
        }

        /* Enqueue on notification wait queues */
        if (wait_count > 0)
        {
            current_thread->waitany_wait_count = wait_count;
            current_thread->waitany_wait_match_index = WAITANY_NO_MATCH;
            current_thread->waitany_wait_bits = 0;
            current_thread->waitany_active = true;

            for (uint32_t i = 0; i < wait_count; i++)
            {
                current_thread->waitany_wait_ntfns[i] = wait_ntfns[i];
                current_thread->waitany_wait_slots[i].owner = current_thread;
                current_thread->waitany_wait_slots[i].index = wait_ntfn_indices[i];
                current_thread->waitany_wait_slots[i].node.prev = NULL;
                current_thread->waitany_wait_slots[i].node.next = NULL;
                list_add_tail(&current_thread->waitany_wait_slots[i].node,
                              &wait_ntfns[i]->wait_queue.node);
            }
        }

        /* Enqueue on endpoint receiver queues */
        if (ep_wait_count > 0)
        {
            current_thread->waitany_ep_wait_count = ep_wait_count;
            current_thread->waitany_ep_wait_match_index = WAITANY_NO_MATCH;
            current_thread->waitany_ep_wait_active = true;

            for (uint32_t i = 0; i < ep_wait_count; i++)
            {
                current_thread->waitany_wait_eps[i] = wait_eps[i];
                current_thread->waitany_ep_wait_slots[i].owner = current_thread;
                current_thread->waitany_ep_wait_slots[i].index = wait_ep_indices[i];
                current_thread->waitany_ep_wait_slots[i].node.prev = NULL;
                current_thread->waitany_ep_wait_slots[i].node.next = NULL;
                list_add_tail(&current_thread->waitany_ep_wait_slots[i].node,
                              &wait_eps[i]->receiver_queue.node);
            }
        }

        current_thread->wake_reason = WAKE_NONE;
        current_thread->blocked_port = NULL;
        current_thread->state = BLOCKED;

        if (timeout_ms != TIMEOUT_INFINITE)
        {
            current_thread->wake_tick = deadline;
            sleep_queue_insert(current_thread);
        }
        else
        {
            current_thread->wake_tick = 0;
        }

        schedule();

        /* Cancel sleep queue entry if not timed out */
        if (timeout_ms != TIMEOUT_INFINITE && current_thread->wake_reason != WAKE_TIMEOUT &&
            current_thread->timeout_node.prev && current_thread->timeout_node.next)
        {
            list_remove(&current_thread->timeout_node);
        }

        /* ERR_DEAD from cap_destroy */
        if ((int32_t)(*arch_reg(frame, 0)) == ERR_DEAD)
        {
            thread_waitany_clear_waits(current_thread);
            thread_waitany_clear_ep_waits(current_thread);
            (*arch_reg(frame, 0)) = ERR_DEAD;
            return;
        }

        /* Timeout */
        if (current_thread->wake_reason == WAKE_TIMEOUT)
        {
            thread_waitany_clear_waits(current_thread);
            thread_waitany_clear_ep_waits(current_thread);
            continue; /* deadline check at top catches expiry */
        }

        /* Woken by endpoint sender */
        if (current_thread->waitany_ep_wait_match_index != WAITANY_NO_MATCH)
        {
            thread_waitany_clear_waits(current_thread);
            thread_waitany_clear_ep_waits(current_thread);
            if (!CopyToUser((void *)result_ptr, &current_thread->waitany_pending_result,
                              wlen))
            {
                (*arch_reg(frame, 0)) = ERR_BADPTR;
                return;
            }
            (*arch_reg(frame, 0)) = 0;
            return;
        }

        /* Woken by notification */
        if (current_thread->waitany_wait_match_index != WAITANY_NO_MATCH)
        {
            waitany_deliver_notification(current_thread->waitany_wait_match_index,
                                         current_thread->waitany_wait_bits,
                                         &result);
            thread_waitany_clear_waits(current_thread);
            thread_waitany_clear_ep_waits(current_thread);
            if (!CopyToUser((void *)result_ptr, &result, wlen))
            {
                (*arch_reg(frame, 0)) = ERR_BADPTR;
                return;
            }
            (*arch_reg(frame, 0)) = 0;
            return;
        }

        /* Spurious wakeup, retry */
        thread_waitany_clear_waits(current_thread);
        thread_waitany_clear_ep_waits(current_thread);
    }
}
