#include "sys_notif.h"
#include "kernel/proc/process.h"
#include "kernel/sched/sched.h"
#include "kernel/syscall/syscall.h"

extern process_t *current_process;

void ntfn_create(exception_frame_t *frame) {
    int handle = handle_vec_find_free(&current_process->handle_table);
    if (handle < 0) { frame->r[0] = ERR_NOMEM; return; }

    notification_t *ntfn = kmalloc(sizeof(notification_t));  // or slab
    if (!ntfn) { frame->r[0] = ERR_NOMEM; return; }

    ntfn->word = 0;
    list_init(&ntfn->wait_queue);
    ntfn->owner_pid = current_process->pid;

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle);
    entry->type = HANDLE_NOTIFICATION;
    entry->ntfn = ntfn;
    entry->grantable = true;
    frame->r[0] = handle;
}

void ntfn_signal(exception_frame_t *frame) {
    uint32_t handle_idx = frame->r[0];
    uint32_t bits = frame->r[1];

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle_idx);
    if (!entry || entry->type != HANDLE_NOTIFICATION) {
        frame->r[0] = ERR_BADARG; return;
    }

    notification_t *ntfn = entry->ntfn;
    ntfn->word |= bits;

    // Wake one waiter if any
    if (!list_empty(&ntfn->wait_queue)) {
        list_node_t *node = list_pop_front(&ntfn->wait_queue);
        process_t *waiter = container_of(node, process_t, node);

        uint32_t delivered = ntfn->word;
        ntfn->word = 0;  // clear on delivery

        waiter->trap_frame->r[0] = delivered;
        waiter->process_state = PROCESS_READY;
        sched_add(waiter);
    }

    frame->r[0] = 0;
}

void ntfn_wait(exception_frame_t *frame) {
    uint32_t handle_idx = frame->r[0];

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle_idx);
    if (!entry || entry->type != HANDLE_NOTIFICATION) {
        frame->r[0] = ERR_BADARG; return;
    }

    notification_t *ntfn = entry->ntfn;

    if (ntfn->word != 0) {
        // Bits already set, return immediately
        frame->r[0] = ntfn->word;
        ntfn->word = 0;
        return;
    }

    // Block until someone signals
    current_process->process_state = PROCESS_BLOCKED;
    list_add_tail(&current_process->node, &ntfn->wait_queue.node);
    schedule();
    // When we wake, r[0] is already set by ntfn_signal
}

void ntfn_poll(exception_frame_t *frame) {
    uint32_t handle_idx = frame->r[0];

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle_idx);
    if (!entry || entry->type != HANDLE_NOTIFICATION) {
        frame->r[0] = ERR_BADARG; return;
    }

    notification_t *ntfn = entry->ntfn;
    frame->r[0] = ntfn->word;
    ntfn->word = 0;
}