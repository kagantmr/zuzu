#include "sys_notif.h"
#include "kernel/proc/process.h"
#include "kernel/sched/sched.h"
#include "kernel/syscall/syscall.h"

extern thread_t *current_thread;

void ntfn_create(exception_frame_t *frame) {
    handle_t handle = handle_vec_find_free(&current_thread->owner_process->handle_table);
    if (handle < 0) { frame->r[0] = ERR_NOMEM; return; }

    notification_t *ntfn = kmalloc(sizeof(notification_t));  // or slab
    if (!ntfn) { frame->r[0] = ERR_NOMEM; return; }

    ntfn->word = 0;
    list_init(&ntfn->wait_queue);
    ntfn->owner_pid = current_thread->owner_process->pid;
    ntfn->ref_count = 1;
    ntfn->alive = true;

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle);
    entry->type = HANDLE_NOTIFICATION;
    entry->ntfn = ntfn;
    entry->grantable = true;
    frame->r[0] = handle;
}

void ntfn_signal(exception_frame_t *frame) {
    handle_t handle_idx = frame->r[0];
    uint32_t bits = frame->r[1];

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle_idx);
    if (!entry || entry->type != HANDLE_NOTIFICATION) {
        frame->r[0] = ERR_BADARG; return;
    }

    notification_t *ntfn = entry->ntfn;
    if (!ntfn || !ntfn->alive) {
        frame->r[0] = ERR_DEAD;
        return;
    }
    ntfn->word |= bits;

    // Wake one waiter if any
    if (!list_empty(&ntfn->wait_queue)) {
        list_node_t *node = list_pop_front(&ntfn->wait_queue);
        thread_t *waiter = container_of(node, thread_t, recvany_wait_nodes);
        if (!waiter->trap_frame) {
            frame->r[0] = ERR_DEAD;
            return;
        }

        uint32_t delivered = ntfn->word;
        ntfn->word = 0;  // clear on delivery

        waiter->trap_frame->r[0] = delivered;
        uint32_t match_index = RECVANY_NO_MATCH;
        if (waiter->recvany_wait_active) {
            for (uint32_t i = 0; i < waiter->recvany_wait_count; i++) {
                if (waiter->recvany_wait_ntfns[i] == ntfn) {
                    match_index = i;
                    break;
                }
            }
        }
        thread_recvany_clear_waits(waiter);
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

    frame->r[0] = 0;
}

void ntfn_wait(exception_frame_t *frame) {
    handle_t handle_idx = frame->r[0];

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle_idx);
    if (!entry || entry->type != HANDLE_NOTIFICATION) {
        frame->r[0] = ERR_BADARG; return;
    }

    notification_t *ntfn = entry->ntfn;
    if (!ntfn || !ntfn->alive) {
        frame->r[0] = ERR_DEAD;
        return;
    }

    if (ntfn->word != 0) {
        // Bits already set, return immediately
        frame->r[0] = ntfn->word;
        ntfn->word = 0;
        return;
    }

    // Block until someone signals
    current_thread->wake_reason = WAKE_NONE;
    current_thread->blocked_endpoint = NULL;
    current_thread->state = BLOCKED;
    list_add_tail(&current_thread->node, &ntfn->wait_queue.node);
    schedule();
    // When we wake, r[0] is already set by ntfn_signal
}

void ntfn_poll(exception_frame_t *frame) {
    handle_t handle_idx = frame->r[0];

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle_idx);
    if (!entry || entry->type != HANDLE_NOTIFICATION) {
        frame->r[0] = ERR_BADARG; return;
    }

    notification_t *ntfn = entry->ntfn;
    if (!ntfn || !ntfn->alive) {
        frame->r[0] = ERR_DEAD;
        return;
    }
    frame->r[0] = ntfn->word;
    ntfn->word = 0;
}