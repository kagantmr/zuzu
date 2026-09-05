#include "sys_ntfn.h"

#include "core/panic.h"

#include "kernel/sched/sched.h"
#include "kernel/syscall/syscall.h"
#include "kernel/time/tick.h"
#ifdef ZUZU_BENCH
#include "kernel/bench.h"
#endif /* ZUZU_BENCH */

#include "ntfn.h"
#include "handle.h"

#define LOG_FMT(fmt) "(sys_ntfn) " fmt
#include "core/log.h"

void SysNtfnCreate(CpuState *frame)
{
    Handle handle = handle_vec_find_free(&current_thread->owner_process->handle_table);
    if (handle < 0) {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }

    NtfnObj *ntfn = kmalloc(sizeof(NtfnObj)); // or slab
    if (!ntfn) {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }

    ntfn->word = 0;
    list_init(&ntfn->wait_queue);
    ntfn->owner_pid = current_thread->owner_process->pid;
    ntfn->ref_count = 1;
    ntfn->alive = true;

    HandleEntry *entry = handle_vec_get(&current_thread->owner_process->handle_table, (uint32_t)handle);
    if (!entry) {
        kfree(ntfn);
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }
    entry->type = HANDLE_NTFN;
    entry->ntfn = ntfn;
    entry->grantable = true;
    arch_reg_set(frame, 0, handle);
}

void SysNtfnSignal(CpuState *frame)
{
    Handle handle_idx = (Handle)(*arch_reg(frame, 0));
    uint32_t bits = (*arch_reg(frame, 1));

    HandleEntry *entry = handle_vec_get(&current_thread->owner_process->handle_table, (uint32_t)handle_idx);
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
    if (bits & (1u << 31)) {
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

    HandleEntry *entry = handle_vec_get(&current_thread->owner_process->handle_table, (uint32_t)handle_idx);
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
#ifdef ZUZU_BENCH
    /* Stashed on the thread, not a local: schedule() below may not return
     * to this stack frame for a long time (other threads run first), so
     * the matching read has to happen wherever this thread is actually
     * unblocked (kernel/irq/sys_irq.c's relay_handler), not here. */
    current_thread->bench_irq_wait_start = BENCH_BEGIN();
#endif /* ZUZU_BENCH */

    if (timeout_ms != TIMEOUT_INFINITE) {
        Tick ticks = ((uint64_t)timeout_ms * (uint64_t)TICK_HZ) / 1000u;
        if (ticks == 0)
            ticks = 1;
        current_thread->wake_tick = GetTicks() + ticks;
        sleep_queue_insert(current_thread);
    } else {
        current_thread->wake_tick = 0;
    }

    schedule();

    if (timeout_ms != TIMEOUT_INFINITE && current_thread->wake_reason != WAKE_TIMEOUT &&
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
