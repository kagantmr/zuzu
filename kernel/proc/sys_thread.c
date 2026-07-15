#include "sys_thread.h"
#include <arch/context.h>
#include "kernel/syscall/syscall.h"
#include "kernel/sched/sched.h"
#include "zuzu/zuzu.h"
#include <zuzu/tcb.h>

void sys_tmake(arch_regs_t *frame) {
    vaddr_t entry  = (*arch_reg(frame, 0));
    vaddr_t usr_sp = (*arch_reg(frame, 1));
    vaddr_t arg    = (*arch_reg(frame, 2));

    if (!validate_user_ptr(entry, 1)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }
    if (!validate_user_ptr(usr_sp, 4)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    process_t *owner = current_thread->owner_process;
    thread_t *t = thread_create(owner);
    if (!t) {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }

    int slot_idx = tcb_slot_alloc(owner);
    if (slot_idx < 0) {
        thread_destroy(t);
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }

    tdata_t *slot = (tdata_t *)(PA_TO_VA(owner->tcb_page_pa) + (uint32_t)slot_idx * TCB_SLOT_SIZE);
    vaddr_t slot_va = owner->tcb_page_va + (uint32_t)slot_idx * TCB_SLOT_SIZE;

    slot->tid     = t->tid;
    slot->pid     = owner->pid;
    slot->lmsg_buf = (void *)(slot_va + offsetof(tdata_t, buf));   /* points into itself */

    t->thread_info_va = slot_va;
    t->tcb_slot = (uint8_t)slot_idx;
    t->ipc_buf_pa = owner->tcb_page_pa + (uint32_t)slot_idx * TCB_SLOT_SIZE + offsetof(tdata_t, buf);

    // Build the initial kernel stack so the thread enters user mode at `entry`.
    t->kernel_sp = (uint32_t *)arch_thread_user_init(
        (void *)t->kernel_stack_top, (uintptr_t)entry, (uintptr_t)usr_sp,
        USER_ELF_BASE, (uint32_t)arg, 0, &t->trap_frame);
    t->state = READY;
    sched_add(t);

    (*arch_reg(frame, 0)) = (tid_t)t->tid;
}

void sys_tjoin(arch_regs_t *frame) {
    tid_t tid = (*arch_reg(frame, 0));
    thread_t *thread = thread_find_by_tid(tid);
    if (!thread) {
        (*arch_reg(frame, 0)) = ERR_NOENT;
        return;
    }
    if (thread->owner_process != current_thread->owner_process) {
        (*arch_reg(frame, 0)) = ERR_NOPERM;
        return;
    }

    if (thread->state != ZOMBIE) {
        current_thread->owner_process->waiting_for_tid = tid;
        current_thread->state = BLOCKED;
        schedule();

        /* `process_wake_joiners` delivered the exit status into our
         * trap frame before making us READY; do not access `thread`
         * here since it may have been unregistered/freed by the
         * reaper. The return value is already placed in `(*arch_reg(frame, 0))`.
         */
        return;
    }

    /* Thread already a ZOMBIE: read the exit status (no destroy).
     * Ownership of destruction belongs to the thread that performed
     * the quit (tquit) and the scheduler reaper. */
    (*arch_reg(frame, 0)) = thread->exit_status;
}


void sys_tquit(arch_regs_t *frame) {
    int exit_status = (int)(*arch_reg(frame, 0));
    thread_t *t = current_thread;
    process_t *owner = t->owner_process;

    t->exit_status = exit_status;
    process_wake_joiners(t->tid, exit_status);

    if (owner->threads.node.next == &t->process_node &&
        t->process_node.next == &owner->threads.node) {
        // last thread, kill the process
        process_kill(owner, exit_status);
    } else {
        thread_kill(t);
        // remove from process thread list NOW so process_destroy won't see it
        if (t->process_node.prev && t->process_node.next)
            list_remove(&t->process_node);
        sched_defer_destroy_thread(t);
    }

    schedule();
}