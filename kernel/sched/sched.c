#include "sched.h"
#include "kernel/space/space.h"
#include <arch/context.h>
#include <compiler.h>
#include <list.h>

#include "kernel/svc/svc.h"
#include <arch/fpu.h>
#include <arch/thread.h>

#include "kernel/mm/vmm/vmm.h"
#include "kernel/time/tick.h"
#include "types.h"
#include <arch/cpu.h>
#include <arch/timer.h>
#include "core/panic.h"
#include <assert.h>
#include <bitmap.h>
#include <stdint.h>

#define IDLE_STACK_BYTES 1024
#define SLEEP_QUEUE_SIZE 512

static ListHead task_destroy_queue = LIST_HEAD_INIT(task_destroy_queue);
TaskObject *current_task;
TaskObject *fpu_owner = NULL;

volatile uint8_t do_resched = 0;

static TaskObject idle_task; // only kernel_sp is used
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
#include <util/log.h>

static void IdleTask(void) __attribute__((noreturn));
static void SchedArmTimer(void);
static void SchedConsumeDestroyQueue(void);
static void SchedIdleWait(void);

static void IdleTask(void)
{
    on_idle_stack = true;
    for (;;)
    {
        VmmActivateAddrspace(VmmGetKernelAddrspace());
        SchedConsumeDestroyQueue();
        SchedIdleWait();
        Schedule();
    }
}

static void SchedInitIdleTask(void)
{
    VirtAddr sp = (VirtAddr)idle_stack + sizeof(idle_stack);
    sp &= ~(VirtAddr)7U;

    idle_task.kernel_sp = (uint32_t *)arch_thread_kernel_init((void *)sp, IdleTask);
}

void SchedInit(void)
{
    for (uint32_t level = 0; level < SCHED_PRIORITY_LEVELS; level++)
        list_init(&run_queues[level]);
    for (uint32_t level = 0; level < SLEEP_QUEUE_SIZE; level++)
        list_init(&sleep_wheel[level]);

    assert(ArchTimerFreq() >= 250);

    slot_shift = (uint32_t)(63 - __builtin_clzll(ArchTimerFreq() / 250));
    wheel_now_slot = ArchTimerNow() >> slot_shift;
    current_task = NULL;
    on_idle_stack = false;
    SchedInitIdleTask();
}

void SchedAdd(TaskObject *t)
{
    if (!t)
        return;
    if (t->owner && t->owner->frozen) return;
    if (t->node.next || t->node.prev)
        return; // double enqueue guard

    uint32_t priority = t->priority;
    if (priority >= SCHED_PRIORITY_LEVELS)
        priority = SCHED_PRIO_DEFAULT;

    t->queued_prio = (uint8_t)priority;
    list_add_tail(&t->node, &run_queues[priority].node);
    ready_mask |= (1U << priority);

    if (current_task && t->priority > current_task->priority)
    {
        do_resched = 1;
    }
}

void SchedQueueDestroyTask(TaskObject *t)
{
    if (!t)
        return;
    /* Guard against double-enqueue: if node is already linked, skip. */
    if (t->destroy_node.next || t->destroy_node.prev)
    {
        return;
    }
    list_add_tail(&t->destroy_node, &task_destroy_queue.node);
}

static void SchedConsumeDestroyQueue(void)
{
    ListHead deferred = LIST_HEAD_INIT(deferred);

    while (!list_empty(&task_destroy_queue))
    {
        ListNode *node = list_pop_front(&task_destroy_queue);
        if (!node)
            break;
        TaskObject *t = container_of(node, TaskObject, destroy_node);

        if (t == current_task)
        {
            list_add_tail(&t->destroy_node, &deferred.node);
            continue;
        }

        TaskDestroy(t);
    }

    while (!list_empty(&deferred))
    {
        ListNode *node = list_pop_front(&deferred);
        if (!node)
            break;
        TaskObject *t = container_of(node, TaskObject, destroy_node);
        list_add_tail(&t->destroy_node, &task_destroy_queue.node);
    }
}

static bool SchedIsWorkPending(void)
{
    if (do_resched)
        return true;

    for (uint32_t level = 0; level < SCHED_PRIORITY_LEVELS; level++)
    {
        if (!list_empty(&run_queues[level]))
            return true;
    }

    return false;
}

void SchedRemoveSleepQueue(TaskObject *t)
{
    if (t->sleep_slot < 0)
        return;
    if (t->timeout_node.prev && t->timeout_node.next)
        list_remove(&t->timeout_node);
    uint32_t slot = (uint32_t)t->sleep_slot;
    if (list_empty(&sleep_wheel[slot]))
        BitmapClr(wheel_occ, slot);
    t->sleep_slot = -1;
}

void SchedInsertSleepQueue(TaskObject *t)
{
    uint64_t abs_slot = t->wake_deadline >> slot_shift;
    if (abs_slot < wheel_now_slot)
        abs_slot = wheel_now_slot;
    if ((abs_slot - wheel_now_slot) >= SLEEP_QUEUE_SIZE)
    {
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
            TaskObject *t = container_of(node, TaskObject, timeout_node);
            t->sleep_slot = -1;
            if ((t->wake_deadline >> slot_shift) > wheel_now_slot)
            {
                SchedInsertSleepQueue(t);
                continue;
            }

            if (t->trap_frame)
                ArchSetInFrame(t->trap_frame, 0, ERR_TIMEOUT);
            SchedUnblock(t);
            SchedAdd(t);
        }

        BitmapClr(wheel_occ, slot);
    }
}

static void SchedIdleWait(void)
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

static TaskObject *SchedPickNext(void)
{
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--)
    {
        if (ready_mask & (1U << level))
        {
            if (unlikely(list_empty(&run_queues[level])))
            {
                ready_mask &= ~(1U << level);
#ifdef DEBUG
                panic("ready_mask bit %d set on an empty run queue", level);
#endif
                continue;
            }
            ListNode *next_node = list_pop_front(&run_queues[level]);
            if (list_empty(&run_queues[level]))
                ready_mask &= ~(1U << level);
            return container_of(next_node, TaskObject, node);
        }
    }
    return &idle_task;
}

bool __hot SchedAnyCpuTakers(const TaskObject *t)
{
    if (unlikely(!t))
        return false;
    uint32_t priority = t->priority;
    if (unlikely(priority >= SCHED_PRIORITY_LEVELS))
        priority = SCHED_PRIORITY_LEVELS - 1;

    uint32_t at_or_above = ready_mask & ~((1U << priority) - 1U);
    return at_or_above != 0;
}

/* Called from Schedule() (every voluntary reschedule) and directly from
 * the Call direct-handoff path -- one of the hottest functions in the
 * kernel. */
void __hot SchedSwitchNext(TaskObject *next)
{
    TaskObject *prev = current_task;

    if (unlikely(next == &idle_task))
    {
        bool from_idle = (prev == NULL && on_idle_stack);
        current_task = NULL;
        SchedArmTimer(); /* no slice to run out; sleepers still need waking */
        if (from_idle)
        {
            return;
        }
        ContextSwitch(prev, &idle_task);
        return;
    }

    current_task = next;
    current_task->state = RUNNING;
    on_idle_stack = false;

    current_task->slice_deadline =
        ArchTimerNow() + ((uint64_t)current_task->time_slice * (ArchTimerFreq() / TICK_HZ));
    SchedArmTimer();

    if (unlikely(next == prev))
        return;

    if (unlikely(current_task == fpu_owner))
    {
        if (!fpu_access_enabled)
        {
            ArchFpuTrapEnable();
            fpu_access_enabled = true;
        }
    }
    else
    {
        if (fpu_access_enabled)
        {
            arch_fpu_trap_disable();
            fpu_access_enabled = false;
        }
    }

    SpaceObject *prev_proc = prev ? prev->owner : NULL;
    if (unlikely(current_task->owner->as &&
                 (!prev_proc || prev_proc->as != current_task->owner->as)))
    {
        VmmActivateAddrspace(current_task->owner->as);
    }
    arch_set_thread_ptr(current_task);
    ContextSwitch(prev, current_task);
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

static void SchedArmTimer(void)
{
    uint64_t now = ArchTimerNow();
    uint64_t deadline = UINT64_MAX;

    uint32_t k = WheelScanFromNow();
    if (k < SLEEP_QUEUE_SIZE)
        deadline = (wheel_now_slot + k + 1) << slot_shift;

    if (current_task && SchedAnyCpuTakers(current_task) && current_task->slice_deadline < deadline)
        deadline = current_task->slice_deadline;

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

void SchedBlockOn(ListHead *queue, Duration timeout)
{
    if (TIMEOUT_POLL == timeout)
    {
        ArchSetInFrame(current_task->trap_frame, 0, ERR_TIMEOUT);
        return;
    }

    current_task->wait_slot.owner  = current_task;
    list_add_tail(&current_task->wait_slot.node, &queue->node);

    current_task->state = BLOCKED;

    if (TIMEOUT_INFINITE != timeout)
    {
        current_task->wake_deadline = ArchDeadlineFromMs(timeout);
        SchedInsertSleepQueue(current_task);
    }
    else
    {
        current_task->wake_deadline = 0;
    }

    Schedule();
}

void SchedUnblock(TaskObject *t) {
    if (t->wait_slot.node.next)           list_remove(&t->wait_slot.node);
    SchedRemoveSleepQueue(t);
    t->wake_deadline = 0;
    t->ipc_state = IPC_NONE;
    t->blocked_port = NULL;
    t->state = READY;
    // caller does switch
}

void __hot Schedule(void)
{
    if (current_task != NULL && current_task->state == RUNNING)
    {
        current_task->state = READY;
        SchedAdd(current_task);
    }

    SchedDoHousekeeping();

    TaskObject *next = SchedPickNext();
    SchedSwitchNext(next); /* sets the slice deadline and arms the timer */
}

size_t SchedGetReadyQueue(TaskObject **out, size_t max_out)
{
    size_t total = 0;
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--)
    {
        ListNode *node = run_queues[level].node.next;

        while (node != &run_queues[level].node)
        {
            if (out && total < max_out)
            {
                out[total] = container_of(node, TaskObject, node);
            }
            total++;
            node = node->next;
        }
    }

    return total;
}

void SchedRemoveRunQueue(TaskObject *t)
{
    if (!t->node.next || !t->node.prev)
        return;
    uint32_t priority = t->queued_prio;
    list_remove(&t->node);
    if (list_empty(&run_queues[priority]))
        ready_mask &= ~(1U << priority);
}

size_t SchedGetSleepers(TaskObject **out, size_t max_out)
{
    size_t total = 0;
    for (uint32_t s = 0; s < SLEEP_QUEUE_SIZE; s++)
    {
        ListNode *node = sleep_wheel[s].node.next;
        while (node != &sleep_wheel[s].node)
        {
            if (out && total < max_out)
                out[total] = container_of(node, TaskObject, timeout_node);
            total++;
            node = node->next;
        }
    }
    return total;
}

void SchedSetReschedFlag(void) { do_resched = 1; }
