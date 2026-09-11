#include "sched.h"
#include "kernel/proc/process.h"
#include <arch/context.h>
#include <compiler.h>
#include <list.h>

#include "kernel/syscall/syscall.h"
#include <arch/fpu.h>
#include <arch/thread.h>

#include "kernel/mm/vmm.h"
#include "kernel/time/tick.h"
#include "zuzu/types.h"
#include <arch/cpu.h>
#include <arch/timer.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <bitmap.h>

#define IDLE_STACK_BYTES 1024
#define SLEEP_QUEUE_SIZE 512


static ListHead destroy_queue = LIST_HEAD_INIT(destroy_queue);
static ListHead thread_destroy_queue = LIST_HEAD_INIT(thread_destroy_queue);
Thread *current_thread;
Thread *fpu_owner = NULL;

volatile uint8_t do_resched = 0; 

static Thread idle_thread; // only kernel_sp is used
static uint8_t idle_stack[IDLE_STACK_BYTES] __attribute__((aligned(8)));
static bool on_idle_stack;

static ListHead run_queues[SCHED_PRIORITY_LEVELS];

static ListHead sleep_wheel[SLEEP_QUEUE_SIZE];
static uint32_t wheel_occ[BITMAP_WORDS(SLEEP_QUEUE_SIZE)];
static uint64_t wheel_now_slot;
static uint32_t slot_shift;

bool fpu_access_enabled = true;

_Static_assert(SCHED_PRIORITY_LEVELS <= 32, "ready_mask is a uint32_t");
static uint32_t ready_mask = 0;

#define LOG_FMT(fmt) "(sched) " fmt
#include <zuzu/log.h>

static void IdleThread(void) __attribute__((noreturn));
void SchedArmTimer(void);

static void IdleThread(void)
{
    on_idle_stack = true;
    for (;;)
    {
        VmmActivateAddrspace(VmmGetKernelAddrspace());
        SchedReap();
        SchedIdleWait();
        Schedule();
    }
}

static void SchedInitIdleThread(void)
{
    VirtAddr sp = (VirtAddr)idle_stack + sizeof(idle_stack);
    sp &= ~(VirtAddr)7U;

    idle_thread.kernel_sp = (uint32_t *)arch_thread_kernel_init((void *)sp, IdleThread);
    idle_thread.state = RUNNING;
}

void SchedInit()
{
    for (uint32_t level = 0; level < SCHED_PRIORITY_LEVELS; level++)
        list_init(&run_queues[level]);
    for (uint32_t level = 0; level < SLEEP_QUEUE_SIZE; level++)
        list_init(&sleep_wheel[level]);

    assert(ArchTimerFreq() >= 250);

    slot_shift = (uint32_t)(63 - __builtin_clzll(ArchTimerFreq() / 250));
    wheel_now_slot = ArchTimerNow() >> slot_shift;
    list_init(&destroy_queue);
    current_thread = NULL;
    on_idle_stack = false;
    SchedInitIdleThread();
}

void SchedAdd(Thread *t)
{
    if (!t)
        return;

    if (t->node.next || t->node.prev)
        return; // double enqueue guard

    uint32_t priority = t->priority;
    if (priority >= SCHED_PRIORITY_LEVELS)
        priority = SCHED_PRIO_DEFAULT;

    list_add_tail(&t->node, &run_queues[priority].node);
    ready_mask |= (1U << priority);

    if (current_thread && t->priority > current_thread->priority)
    {
        do_resched = 1;
    }
}

void SchedQueueDestroyProcess(ProcessObj *p) { list_add_tail(&p->destroy_node, &destroy_queue.node); }

void SchedQueueDestroyThread(Thread *t)
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

void SchedConsumeDestroyQueue(void)
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

void SchedReap(void)
{
    /* Removed noisy debug logging to avoid flooding the console. */
    while (!list_empty(&destroy_queue))
    {
        ListNode *node = list_pop_front(&destroy_queue);
        ProcessObj *p = container_of(node, ProcessObj, destroy_node);
        ProcessDestroy(p);
    }
    SchedConsumeDestroyQueue();
}

static bool SchedIsWorkPending(void)
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

void SchedRemoveSleepQueue(Thread *t) {
    if (t->sleep_slot < 0) return;
    if (t->timeout_node.prev && t->timeout_node.next) list_remove(&t->timeout_node);
    uint32_t slot = (uint32_t)t->sleep_slot;
    if (list_empty(&sleep_wheel[slot])) BitmapClr(wheel_occ, slot);
    t->sleep_slot = -1;
}

void SchedInsertSleepQueue(Thread *t)
{
    uint64_t abs_slot = t->wake_deadline >> slot_shift;
    if (abs_slot < wheel_now_slot) abs_slot = wheel_now_slot;
    if ((abs_slot - wheel_now_slot) >= SLEEP_QUEUE_SIZE) {
        abs_slot = wheel_now_slot + SLEEP_QUEUE_SIZE - 1;
    }
    uint32_t slot = (uint32_t)(abs_slot % SLEEP_QUEUE_SIZE);
    list_add_tail(&t->timeout_node, &sleep_wheel[slot].node);
    BitmapSet(wheel_occ, slot);
    t->sleep_slot = (int16_t)slot;
    SchedArmTimer();
}

static void SchedWakeSleepers(void)
{
    uint64_t now_slot = ArchTimerNow() >> slot_shift;
    if (now_slot - wheel_now_slot > SLEEP_QUEUE_SIZE)
        wheel_now_slot = now_slot - SLEEP_QUEUE_SIZE;

    for (; wheel_now_slot < now_slot; wheel_now_slot++)
    {
        uint32_t slot = (uint32_t)(wheel_now_slot % SLEEP_QUEUE_SIZE);
        ListHead *bucket = &sleep_wheel[slot];

        for (;;)
        {
            ListNode *node = list_pop_front(bucket);
            if (!node)
                break;
            Thread *t = container_of(node, Thread, timeout_node);
            t->sleep_slot = -1;
            if ((t->wake_deadline >> slot_shift) > wheel_now_slot) {
                SchedInsertSleepQueue(t);
                continue;
            }


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
                SchedAdd(t);
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
                t->wake_deadline = 0;
                SchedAdd(t);
            }
        }

        BitmapClr(wheel_occ, slot);
    }
}

void SchedIdleWait(void)
{
    for (;;)
    {
        arch_global_irq_disable();

        if (SchedIsWorkPending())
        {
            if (do_resched)
                do_resched = 0;
            arch_global_irq_enable();
            return;
        }

        __asm__ volatile("wfi" ::: "memory");
        arch_global_irq_enable();

        if (SchedIsWorkPending())
        {
            if (do_resched)
                do_resched = 0;
            return;
        }
    }
}

static void SchedDoHousekeeping(void)
{
    SchedConsumeDestroyQueue();
    SchedWakeSleepers();
}

static Thread *SchedPickNext(void)
{
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--)
    {
        if (ready_mask & (1U << level))
        {
            ListNode *next_node = list_pop_front(&run_queues[level]);
            if (list_empty(&run_queues[level]))
                ready_mask &= ~(1U << level);
            return container_of(next_node, Thread, node);
        }
    }
    return &idle_thread;
}

bool __hot SchedAnyCpuTakers(const Thread *t)
{
    if (unlikely(!t))
        return false;
    uint32_t priority = t->priority;
    if (unlikely(priority >= SCHED_PRIORITY_LEVELS))
        priority = SCHED_PRIORITY_LEVELS - 1;

    uint32_t at_or_above = ready_mask & ~((1U << priority) - 1U);
    return at_or_above != 0;
}

/* Called from schedule() (every voluntary reschedule) and directly from
 * SysMsgCall's/SysMsgLcall's direct-handoff path -- one of the hottest
 * functions in the kernel. */
void __hot SchedSwitchNext(Thread *next)
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

    if (unlikely(current_thread == fpu_owner)) {
        if (!fpu_access_enabled) {
            arch_fpu_trap_enable();
            fpu_access_enabled = true;
        }
    } else {
        if (fpu_access_enabled) {
            arch_fpu_trap_disable();
            fpu_access_enabled = false;
        }
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

static uint32_t WheelScanFromNow(void)
{
    uint32_t start = (uint32_t)(wheel_now_slot % SLEEP_QUEUE_SIZE);
    for (uint32_t i = 0; i < SLEEP_QUEUE_SIZE; i++)
    {
        uint32_t slot = start + i;
        if (slot >= SLEEP_QUEUE_SIZE)
            slot -= SLEEP_QUEUE_SIZE;
        if (wheel_occ[slot >> 5] & (1U << (slot & 31)))
            return i;
    }
    return SLEEP_QUEUE_SIZE;
}

void SchedArmTimer(void)
{
    uint64_t now = ArchTimerNow();
    uint64_t deadline = UINT64_MAX;

    uint32_t k = WheelScanFromNow();
    if (k < SLEEP_QUEUE_SIZE)
        deadline = (wheel_now_slot + k + 1) << slot_shift;

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

void __hot Schedule(void)
{
    if (current_thread != NULL && current_thread->state == RUNNING)
    {
        current_thread->state = READY;
        SchedAdd(current_thread);
    }

    SchedDoHousekeeping();

    Thread *next = SchedPickNext();
    SchedSwitchNext(next); /* sets the slice deadline and arms the timer */
}

size_t SchedGetReadyQueue(Thread **out, size_t max_out)
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

size_t SchedGetSleepers(Thread **out, size_t max_out)
{
    size_t total = 0;
    for (uint32_t s = 0; s < SLEEP_QUEUE_SIZE; s++)
    {
        ListNode *node = sleep_wheel[s].node.next;
        while (node != &sleep_wheel[s].node)
        {
            if (out && total < max_out)
                out[total] = container_of(node, Thread, timeout_node);
            total++;
            node = node->next;
        }
    }
    return total;
}

void SchedSetReschedFlag(void)
{
    do_resched = 1;
}
