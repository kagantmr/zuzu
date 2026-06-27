#include "sys_notif.h"
#include "kernel/proc/process.h"
#include "kernel/sched/sched.h"
#include "kernel/syscall/syscall.h"
#include "kernel/time/tick.h"

extern thread_t *current_thread;

void ntfn_create(arch_regs_t *frame) {
    handle_t handle = handle_vec_find_free(&current_thread->owner_process->handle_table);
    if (handle < 0) { (*arch_reg(frame, 0)) = ERR_NOMEM; return; }

    notification_t *ntfn = kmalloc(sizeof(notification_t));  // or slab
    if (!ntfn) { (*arch_reg(frame, 0)) = ERR_NOMEM; return; }

    ntfn->word = 0;
    list_init(&ntfn->wait_queue);
    ntfn->owner_pid = current_thread->owner_process->pid;
    ntfn->ref_count = 1;
    ntfn->alive = true;

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle);
    entry->type = HANDLE_NOTIFICATION;
    entry->ntfn = ntfn;
    entry->grantable = true;
    (*arch_reg(frame, 0)) = handle;
}

void ntfn_signal(arch_regs_t *frame) {
    handle_t handle_idx = (*arch_reg(frame, 0));
    uint32_t bits = (*arch_reg(frame, 1));

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle_idx);
    if (!entry || entry->type != HANDLE_NOTIFICATION) {
        (*arch_reg(frame, 0)) = ERR_BADARG; return;
    }

    notification_t *ntfn = entry->ntfn;
    if (!ntfn || !ntfn->alive) {
        (*arch_reg(frame, 0)) = ERR_DEAD;
        return;
    }
    ntfn->word |= bits;

    // Wake one waiter if any
    if (!list_empty(&ntfn->wait_queue)) {
        list_node_t *node = list_pop_front(&ntfn->wait_queue);
        thread_wait_slot_t *slot = container_of(node, thread_wait_slot_t, node);
        thread_t *waiter = slot->owner;
        if (!waiter->trap_frame) {
            (*arch_reg(frame, 0)) = ERR_DEAD;
            return;
        }

        uint32_t delivered = ntfn->word;
        ntfn->word = 0;  // clear on delivery

        (*arch_reg(waiter->trap_frame, 0)) = delivered;
        uint32_t match_index = RECVANY_NO_MATCH;
        if (waiter->recvany_wait_active) {
            for (uint32_t i = 0; i < waiter->recvany_wait_count; i++) {
                if (waiter->recvany_wait_ntfns[i] == ntfn) {
                    match_index = waiter->recvany_wait_slots[i].index;
                    break;
                }
            }
        }
        thread_recvany_clear_waits(waiter);
        thread_recvany_clear_ep_waits(waiter);
        waiter->recvany_wait_match_index = match_index;
        waiter->recvany_wait_bits = delivered;
        if (waiter->wake_tick != 0 && waiter->timeout_node.prev && waiter->timeout_node.next) {
            list_remove(&waiter->timeout_node);
        }
        waiter->wake_tick = 0;
        waiter->wake_reason = WAKE_IPC;
        waiter->blocked_endpoint = NULL;
        waiter->ipc_state = IPC_NONE;
        waiter->state = READY;
        sched_add(waiter);
    }

    (*arch_reg(frame, 0)) = 0;
}

void ntfn_wait(arch_regs_t *frame) {
    handle_t handle_idx = (*arch_reg(frame, 0));
    uint32_t timeout_ms = (*arch_reg(frame, 1));

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle_idx);
    if (!entry || entry->type != HANDLE_NOTIFICATION) {
        (*arch_reg(frame, 0)) = ERR_BADARG; return;
    }

    notification_t *ntfn = entry->ntfn;
    if (!ntfn || !ntfn->alive) {
        (*arch_reg(frame, 0)) = ERR_DEAD;
        return;
    }

    if (ntfn->word != 0) {
        (*arch_reg(frame, 0)) = ntfn->word;
        ntfn->word = 0;
        return;
    }

    if (timeout_ms == 0) {
        (*arch_reg(frame, 0)) = 0;
        return;
    }

    current_thread->wake_reason = WAKE_NONE;
    current_thread->blocked_endpoint = NULL;
    current_thread->state = BLOCKED;
    current_thread->ntfn_wait_slot.owner = current_thread;
    current_thread->ntfn_wait_slot.index = 0;
    current_thread->ntfn_wait_slot.node.prev = NULL;
    current_thread->ntfn_wait_slot.node.next = NULL;
    list_add_tail(&current_thread->ntfn_wait_slot.node, &ntfn->wait_queue.node);

    if (timeout_ms != UINT32_MAX) {
        tick_t ticks = ((uint64_t)timeout_ms * (uint64_t)TICK_HZ) / 1000u;
        if (ticks == 0) ticks = 1;
        current_thread->wake_tick = get_ticks() + ticks;
        sleep_queue_insert(current_thread);
    } else {
        current_thread->wake_tick = 0;
    }

    schedule();

    if (timeout_ms != UINT32_MAX && current_thread->wake_reason != WAKE_TIMEOUT &&
        current_thread->timeout_node.prev && current_thread->timeout_node.next) {
        list_remove(&current_thread->timeout_node);
    }

    if (current_thread->wake_reason == WAKE_TIMEOUT) {
        if (current_thread->ntfn_wait_slot.node.prev && current_thread->ntfn_wait_slot.node.next) {
            list_remove(&current_thread->ntfn_wait_slot.node);
        }
        // r[0] already set to ERR_TIMEOUT by scheduler
        return;
    }
    // When woken by ntfn_signal, r[0] is already set by ntfn_signal
}