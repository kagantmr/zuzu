#include "sys_ntfn.h"

#include "kernel/sched/sched.h"
#include "kernel/svc/svc.h"
#include <arch/timer.h>
#ifdef CONFIG_ZUZU_BENCH
#include "kernel/bench.h"
#endif /* CONFIG_ZUZU_BENCH */

#include "event.h"
#include "kernel/space/space.h"

#define LOG_FMT(fmt) "(sys_ntfn) " fmt
#include <zuzu/log.h>


void SysNtfnWait(CpuState *frame)
{
    Handle handle_idx = (Handle)(*ArchGetFromFrame(frame, 0));
    uint32_t timeout_ms = (uint32_t)(*ArchGetFromFrame(frame, 1));

    HandleTableEntry *entry = HandleTableGet(&CURRENT_SPACE->handle_table, handle_idx);
    if (!entry) {
        ArchSetInFrame(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type != HANDLE_EVENT) {
        ArchSetInFrame(frame, 0, ERR_BADTYPE);
        return;
    }

    EventObject *ev = entry->event;
    if (!ev || !ev->alive) {
        ArchSetInFrame(frame, 0, ERR_DEAD);
        return;
    }

    if (ev->word != 0) {
        /* bits are 31-bit (signal rejects bit 31), so this is never negative */
        ArchSetInFrame(frame, 0, (int)ev->word);
        ev->word = 0;
        return;
    }

    if (timeout_ms == TIMEOUT_POLL) {
        ArchSetInFrame(frame, 0, ERR_TIMEOUT);
        return;
    }

    current_task->wake_reason = WAKE_NONE;
    current_task->blocked_port = NULL;
    current_task->state = BLOCKED;
    current_task->ntfn_wait_slot.owner = current_task;
    current_task->ntfn_wait_slot.node.prev = NULL;
    current_task->ntfn_wait_slot.node.next = NULL;
    list_add_tail(&current_task->ntfn_wait_slot.node, &ev->wait_queue.node);
#ifdef CONFIG_ZUZU_BENCH
    /* Stashed on the thread, not a local: schedule() below may not return
     * to this stack frame for a long time (other threads run first), so
     * the matching read has to happen wherever this thread is actually
     * unblocked (kernel/irq/sys_irq.c's relay_handler), not here. */
    current_task->bench_irq_wait_start = BENCH_BEGIN();
#endif /* CONFIG_ZUZU_BENCH */

    if (timeout_ms != TIMEOUT_INFINITE) {
        current_task->wake_deadline = ArchDeadlineFromMs(timeout_ms);
        SchedInsertSleepQueue(current_task);
    } else {
        current_task->wake_deadline = 0;
    }

    Schedule();

    if (timeout_ms != TIMEOUT_INFINITE && current_task->wake_reason != WAKE_TIMEOUT) {
        SchedRemoveSleepQueue(current_task);
    }

    if (current_task->wake_reason == WAKE_TIMEOUT) {
        if (current_task->ntfn_wait_slot.node.prev && current_task->ntfn_wait_slot.node.next) {
            list_remove(&current_task->ntfn_wait_slot.node);
        }
        // r[0] already set to ERR_TIMEOUT by scheduler
        return;
    }
    // When woken by ntfn_signal, r[0] is already set by ntfn_signal
}
