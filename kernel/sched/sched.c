#include "sched.h"
#include "kernel/proc/process.h"
#include <arch/context.h>
#include <compiler.h>
#include <list.h>

#include "kernel/syscall/syscall.h"
#include <arch/fpu.h>
#include <arch/thread.h>

#include "kernel/mm/alloc.h"
#include "kernel/mm/vmm.h"
#include "kernel/time/tick.h"
#include <arch/cpu.h>
#include <arch/timer.h>
#include <stdint.h>
#include <string.h>

static __always_inline uint32_t thread_priority(const Thread *t)
{
    if (unlikely(!t))
        return 0;

    return t->priority;
}

static ListHead destroy_queue = LIST_HEAD_INIT(destroy_queue);
ListHead sleep_queue = LIST_HEAD_INIT(sleep_queue);
static ListHead thread_destroy_queue = LIST_HEAD_INIT(thread_destroy_queue);
Thread *current_thread;

Thread *fpu_owner = NULL;

volatile uint8_t do_resched = 0; // needs spinlock guard on SMP

static Thread idle_thread; // only kernel_sp is used
static uint8_t idle_stack[4096] __attribute__((aligned(8)));
static bool on_idle_stack;

static ListHead run_queues[SCHED_PRIORITY_LEVELS];

_Static_assert(SCHED_PRIORITY_LEVELS <= 32, "ready_mask is a uint32_t");
static uint32_t ready_mask = 0;

#define LOG_FMT(fmt) "(sched) " fmt
#include "core/log.h"

static void sched_idle_trampoline(void) __attribute__((noreturn));
void SchedArmTimer(void);

static void sched_idle_trampoline(void)
{
    on_idle_stack = true;
    for (;;)
    {
        VmmActivateAddrspace(VmmGetKernelAddrspace());
        sched_reap();
        sched_idle_wait();
        schedule();
    }
}

static void sched_init_idle_context(void)
{
    uintptr_t sp = (uintptr_t)idle_stack + sizeof(idle_stack);
    sp &= ~(uintptr_t)7u;

    idle_thread.kernel_sp = (uint32_t *)arch_thread_kernel_init((void *)sp, sched_idle_trampoline);
    idle_thread.state = RUNNING;
}

void sched_init()
{
    for (uint32_t level = 0; level < SCHED_PRIORITY_LEVELS; level++)
        list_init(&run_queues[level]);
    list_init(&destroy_queue);
    list_init(&sleep_queue);
    current_thread = NULL;
    on_idle_stack = false;
    sched_init_idle_context();
}
void sched_add(Thread *t)
{
    if (!t)
        return;

    if (t->node.next || t->node.prev)
        return; // double enqueue guard

    uint32_t priority = thread_priority(t);
    if (priority >= SCHED_PRIORITY_LEVELS)
        priority = SCHED_PRIO_DEFAULT;

    list_add_tail(&t->node, &run_queues[priority].node);
    ready_mask |= (1u << priority);

    if (current_thread && t->priority > current_thread->priority)
    {
        do_resched = 1;
    }
}

void sched_defer_destroy(ProcessObj *p) { list_add_tail(&p->destroy_node, &destroy_queue.node); }

void sched_defer_destroy_thread(Thread *t)
{
    if (!t)
        return;
    /* Guard against double-enqueue: if node is already linked, skip. */
    if (t->destroy_node.next || t->destroy_node.prev)
    {
        return;
    }
    list_add_tail(&t->destroy_node, &thread_destroy_queue.node);
}

void sched_reap_thread_destroys(void)
{
    ListHead deferred = LIST_HEAD_INIT(deferred);

    while (!list_empty(&thread_destroy_queue))
    {
        ListNode *node = list_pop_front(&thread_destroy_queue);
        if (!node)
            break;
        Thread *t = container_of(node, Thread, destroy_node);

        if (t == current_thread)
        {
            list_add_tail(&t->destroy_node, &deferred.node);
            continue;
        }

        ThreadDestroy(t);
    }

    while (!list_empty(&deferred))
    {
        ListNode *node = list_pop_front(&deferred);
        if (!node)
            break;
        Thread *t = container_of(node, Thread, destroy_node);
        list_add_tail(&t->destroy_node, &thread_destroy_queue.node);
    }
}

void sched_reap(void)
{
    /* Removed noisy debug logging to avoid flooding the console. */
    while (!list_empty(&destroy_queue))
    {
        ListNode *node = list_pop_front(&destroy_queue);
        ProcessObj *p = container_of(node, ProcessObj, destroy_node);
        ProcessDestroy(p);
    }
    sched_reap_thread_destroys();
}

static bool sched_work_pending(void)
{
    if (do_resched || !list_empty(&destroy_queue))
        return true;

    for (uint32_t level = 0; level < SCHED_PRIORITY_LEVELS; level++)
    {
        if (!list_empty(&run_queues[level]))
            return true;
    }

    return false;
}

void sleep_queue_insert(Thread *t)
{
    ListNode *curr;
    list_for_each(curr, &sleep_queue.node)
    {
        Thread *s = container_of(curr, Thread, timeout_node);
        if (t->wake_tick < s->wake_tick)
        {
            list_insert_before(&t->timeout_node, curr);
            SchedArmTimer();
            return;
        }
    }
    list_add_tail(&t->timeout_node, &sleep_queue.node);
    SchedArmTimer();
}

static void sched_wake_sleepers(void)
{
    uint64_t now = GetTicks();
    while (!list_empty(&sleep_queue))
    {
        ListNode *head = sleep_queue.node.next;
        Thread *t = container_of(head, Thread, timeout_node);
        if (t->wake_tick > now)
            break;
        list_remove(&t->timeout_node);
        if (t->ipc_state == IPC_RECEIVER || t->ipc_state == IPC_SENDER)
        {
            if (t->ipc_state == IPC_SENDER)
            {
                if (t->node.prev && t->node.next)
                    list_remove(&t->node);
            }
            else
            {
                if (t->port_wait_slot.node.prev && t->port_wait_slot.node.next)
                    list_remove(&t->port_wait_slot.node);
            }
            t->ipc_state = IPC_NONE;
            t->blocked_port = NULL;
            t->wake_reason = WAKE_TIMEOUT;
            arch_reg_set(t->trap_frame, 0, ERR_TIMEOUT);
            t->state = READY;
            sched_add(t);
        }
        else
        {
            t->wake_reason = WAKE_TIMEOUT;
            if (t->trap_frame)
                arch_reg_set(t->trap_frame, 0, ERR_TIMEOUT);
            ThreadWaitanyClearWaits(t);
            ThreadWaitanyClearPortWaits(t);
            if (t->ntfn_wait_slot.node.prev && t->ntfn_wait_slot.node.next)
                list_remove(&t->ntfn_wait_slot.node);
            t->state = READY;
            t->wake_tick = 0;
            sched_add(t);
        }
    }
}

void sched_idle_wait(void)
{
    for (;;)
    {
        arch_global_irq_disable();

        if (sched_work_pending())
        {
            if (do_resched)
                do_resched = 0;
            arch_global_irq_enable();
            return;
        }

        __asm__ volatile("wfi" ::: "memory");
        arch_global_irq_enable();

        if (sched_work_pending())
        {
            if (do_resched)
                do_resched = 0;
            return;
        }
    }
}

static void sched_housekeeping(void)
{
    sched_reap_thread_destroys();
    sched_wake_sleepers();
}

static Thread *sched_pick_next(void)
{
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--)
    {
        if (ready_mask & (1u << level))
        {
            ListNode *next_node = list_pop_front(&run_queues[level]);
            if (list_empty(&run_queues[level]))
                ready_mask &= ~(1u << level);
            return container_of(next_node, Thread, node);
        }
    }
    return &idle_thread;
}

bool __hot SchedAnyCpuTakers(const Thread *t)
{
    uint32_t priority = thread_priority(t);
    if (unlikely(priority >= SCHED_PRIORITY_LEVELS))
        priority = SCHED_PRIORITY_LEVELS - 1;

    uint32_t at_or_above = ready_mask & ~((1u << priority) - 1u);
    return at_or_above != 0;
}

/* Called from schedule() (every voluntary reschedule) and directly from
 * SysMsgCall's/SysMsgLcall's direct-handoff path -- one of the hottest
 * functions in the kernel. */
void __hot switch_to_thread(Thread *next)
{
    Thread *prev = current_thread;

    if (unlikely(next == &idle_thread))
    {
        bool from_idle = (prev == NULL && on_idle_stack);
        current_thread = NULL;
        SchedArmTimer(); /* no slice to run out; sleepers still need waking */
        if (from_idle)
        {
            return;
        }
        context_switch(prev, &idle_thread);
        return;
    }

    current_thread = next;
    current_thread->state = RUNNING;
    on_idle_stack = false;

    current_thread->ticks_remaining = current_thread->time_slice;
    current_thread->slice_deadline =
        ArchTimerNow() + ((uint64_t)current_thread->time_slice * (ArchTimerFreq() / TICK_HZ));
    SchedArmTimer();

    if (unlikely(next == prev))
        return;


    if (unlikely(current_thread == fpu_owner))
    {
        arch_fpu_trap_enable();
    }
    else
    {
        arch_fpu_trap_disable();
    }

    ProcessObj *prev_proc = prev ? prev->owner_process : NULL;
    if (unlikely(current_thread->owner_process->as &&
                 (!prev_proc || prev_proc->as != current_thread->owner_process->as)))
    {
        VmmActivateAddrspace(current_thread->owner_process->as);
    }
    arch_set_thread_ptr(current_thread);
    context_switch(prev, current_thread);
}

#define MIN_TIMER_SLACK (ArchTimerFreq() / 500000u) /* 2us, any CNTFRQ */

void SchedArmTimer(void)
{
    uint64_t now = ArchTimerNow();
    uint64_t deadline = UINT64_MAX;

    /* earliest sleeper */
    if (!list_empty(&sleep_queue))
    {
        Thread *head = container_of(sleep_queue.node.next, Thread, timeout_node);
        uint64_t d = head->wake_tick * (uint64_t)(ArchTimerFreq() / TICK_HZ);
        if (d < deadline)
            deadline = d;
    }

    if (current_thread && SchedAnyCpuTakers(current_thread) &&
        current_thread->slice_deadline < deadline)
        deadline = current_thread->slice_deadline;

    if (deadline == UINT64_MAX)
    {
        /* Nothing sleeping and no peer to preempt for: no wakeup needed.
         * A device IRQ still wakes the CPU from WFI. */
        ArchTimerDisable();
        return;
    }
    if (deadline <= now + MIN_TIMER_SLACK)
        deadline = now + MIN_TIMER_SLACK;

    ArchTimerSetDeadline(deadline);
}

void __hot schedule(void)
{
    if (current_thread != NULL && current_thread->state == RUNNING)
    {
        current_thread->state = READY;
        sched_add(current_thread);
    }

    sched_housekeeping();

    Thread *next = sched_pick_next();
    switch_to_thread(next); /* sets the slice deadline and arms the timer */
}

size_t sched_ready_queue_snapshot(Thread **out, size_t max_out)
{
    size_t total = 0;
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--)
    {
        ListNode *node = run_queues[level].node.next;

        while (node != &run_queues[level].node)
        {
            if (out && total < max_out)
            {
                out[total] = container_of(node, Thread, node);
            }
            total++;
            node = node->next;
        }
    }

    return total;
}

void set_resched_flag(void)
{

    if (current_thread)
    {
        if (ArchTimerNow() >= current_thread->slice_deadline)
        {
            do_resched = 1;
        }
    }
    else
    {
        do_resched = 1; // idling
    }
}
