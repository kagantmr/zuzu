#include "sys_ntfn.h"

#include "kernel/sched/sched.h"
#include "kernel/syscall/syscall.h"
#include <arch/timer.h>
#ifdef CONFIG_ZUZU_BENCH
#include "kernel/bench.h"
#endif /* CONFIG_ZUZU_BENCH */

#include "ntfn.h"
#include "handle.h"

#define LOG_FMT(fmt) "(sys_ntfn) " fmt
#include <zuzu/log.h>

void SysNtfnCreate(CpuState *frame)
{
    HandleTable *ht = &current_thread->owner_process->handle_table;
    Handle handle = HandleTableFindFree(ht);
    if (handle < 0) {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }

    NtfnObj *ntfn = KAllocNtfn();
    if (!ntfn) {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }

    ntfn->word = 0;
    list_init(&ntfn->wait_queue);
    ntfn->owner_pid = current_thread->owner_process->pid;
    ntfn->ref_count = 1;
    ntfn->alive = true;

    HandleEntry *entry = HandleTableGet(ht, (uint32_t)handle);
    if (!entry) {
        KFreeNtfn(ntfn);
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }
    entry->type = HANDLE_NTFN;
    entry->ntfn = ntfn;
    entry->grantable = true;
    HandleEntryClaim(ht, entry);
    arch_reg_set(frame, 0, handle);
}

void SysNtfnSignal(CpuState *frame)
{
    Handle handle_idx = (Handle)(*arch_reg(frame, 0));
    uint32_t bits = (*arch_reg(frame, 1));

    HandleEntry *entry = HandleTableGet(&current_thread->owner_process->handle_table, (uint32_t)handle_idx);
    if (!entry) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type != HANDLE_NTFN) {
        arch_reg_set(frame, 0, ERR_BADTYPE);
        return;
    }

    NtfnObj *ntfn = entry->ntfn;
    if (!ntfn || !ntfn->alive) {
        arch_reg_set(frame, 0, ERR_DEAD);
        return;
    }
    /* bit 31 reserved: bits ride in r0, negatives are errors */
    if (bits & (1U << 31)) {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }

    NtfnSignal(ntfn, bits);

    (*arch_reg(frame, 0)) = 0;
}

void SysNtfnWait(CpuState *frame)
{
    Handle handle_idx = (Handle)(*arch_reg(frame, 0));
    uint32_t timeout_ms = (*arch_reg(frame, 1));

    HandleEntry *entry = HandleTableGet(&current_thread->owner_process->handle_table, (uint32_t)handle_idx);
    if (!entry) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type != HANDLE_NTFN) {
        arch_reg_set(frame, 0, ERR_BADTYPE);
        return;
    }

    NtfnObj *ntfn = entry->ntfn;
    if (!ntfn || !ntfn->alive) {
        arch_reg_set(frame, 0, ERR_DEAD);
        return;
    }

    if (ntfn->word != 0) {
        /* bits are 31-bit (signal rejects bit 31), so this is never negative */
        (*arch_reg(frame, 0)) = ntfn->word;
        ntfn->word = 0;
        return;
    }

    if (timeout_ms == TIMEOUT_POLL) {
        arch_reg_set(frame, 0, ERR_TIMEOUT);
        return;
    }

    current_thread->wake_reason = WAKE_NONE;
    current_thread->blocked_port = NULL;
    current_thread->state = BLOCKED;
    current_thread->ntfn_wait_slot.owner = current_thread;
    current_thread->ntfn_wait_slot.index =
        0; /* unused on the plain-wait path; only waitany reads index */
    current_thread->ntfn_wait_slot.node.prev = NULL;
    current_thread->ntfn_wait_slot.node.next = NULL;
    list_add_tail(&current_thread->ntfn_wait_slot.node, &ntfn->wait_queue.node);
#ifdef CONFIG_ZUZU_BENCH
    /* Stashed on the thread, not a local: schedule() below may not return
     * to this stack frame for a long time (other threads run first), so
     * the matching read has to happen wherever this thread is actually
     * unblocked (kernel/irq/sys_irq.c's relay_handler), not here. */
    current_thread->bench_irq_wait_start = BENCH_BEGIN();
#endif /* CONFIG_ZUZU_BENCH */

    if (timeout_ms != TIMEOUT_INFINITE) {
        current_thread->wake_deadline = ArchDeadlineFromMs(timeout_ms);
        SchedInsertSleepQueue(current_thread);
    } else {
        current_thread->wake_deadline = 0;
    }

    Schedule();

    if (timeout_ms != TIMEOUT_INFINITE && current_thread->wake_reason != WAKE_TIMEOUT) {
        SchedRemoveSleepQueue(current_thread);
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
