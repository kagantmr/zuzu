/**
 * @file sched.c
 * @brief Implementation of the scheduler: run queues, the sleep wheel and context switching.
 */

#include "sched.h"
#include "kernel/space/space.h"
#include <arch/context.h>
#include <compiler.h>
#include <list.h>

#include "kernel/svc/svc.h"
#include <arch/fpu.h>
#include <arch/thread.h>

#include "core/panic.h"
#include "kernel/bench.h"
#include "kernel/mm/vmm/vmm.h"
#include "kernel/clock.h"
#include "types.h"
#include <arch/cpu.h>
#include <arch/timer.h>
#include <assert.h>
#include <bitmap.h>
#include <stdint.h>

/** @brief Size of the stack the idle loop runs on. */
#define IDLE_STACK_BYTES 1024
/** @brief Number of slots in the sleep wheel. */
#define SLEEP_QUEUE_SIZE 512

#ifdef CONFIG_ZUZU_BENCH
/** @brief Time taken to destroy one reaped task. */
BENCH_STAT(g_bench_reap_task, "sched: reap one task");
#endif

/** @brief Tasks that have exited and wait to be destroyed off their own stack. */
static ListHead task_destroy_queue = LIST_HEAD_INIT(task_destroy_queue);
/** @brief The running task, or NULL while the idle loop runs. */
TaskObject *current_task;
/** @brief The task whose registers are currently in the FPU. */
TaskObject *fpu_owner = NULL;

/** @brief Reschedule request, checked on interrupt exit. */
volatile bool do_resched = 0;

/** @brief The task run when nothing else is ready. */
static TaskObject idle_task;
/** @brief Stack for the idle task. */
static uint8_t idle_stack[IDLE_STACK_BYTES] __attribute__((aligned(8)));
/** @brief Whether the CPU is running on the idle stack. */
static bool on_idle_stack;

/** @brief Whether the timer is currently set for the running task's slice end. */
static bool slice_armed;
/** @brief Set while the timer interrupt handler is running. */
static bool in_tick;
/** @brief One FIFO queue of ready tasks per priority level. */
static ListHead run_queues[SCHED_PRIORITY_LEVELS];

/** @brief Tasks with a timeout, bucketed by the slot their deadline falls in. */
static ListHead sleep_wheel[SLEEP_QUEUE_SIZE];
/** @brief One bit per sleep wheel slot, set while the slot has tasks. */
static uint32_t wheel_occ[BITMAP_WORDS(SLEEP_QUEUE_SIZE)];
/** @brief The earliest deadline in each sleep wheel slot. */
static uint64_t wheel_min[SLEEP_QUEUE_SIZE];
/** @brief The sleep wheel slot that time has reached. */
static uint64_t wheel_now_slot;
/** @brief Log2 of the number of timer ticks one sleep wheel slot covers. */
static uint32_t slot_shift;

/** @brief Whether the running task may use the FPU without trapping. */
bool fpu_access_enabled = true;

_Static_assert(SCHED_PRIORITY_LEVELS <= 32, "ready_mask is a uint32_t");
/** @brief One bit per priority level, set while its run queue is not empty. */
static uint32_t ready_mask = 0;

#define LOG_FMT(fmt) "(sched) " fmt
#include <util/log.h>

static void IdleTask(void) __attribute__((noreturn));
static void SchedArmTimer(void);
static void SchedConsumeDestroyQueue(void);
static void SchedIdleWait(void);

/**
 * @brief The idle loop: reap dead tasks, wait for work, then reschedule.
 */
static void IdleTask(void)
{
    on_idle_stack = true;
    for (;;) {
        VmmActivateAddressSpace(VmmGetKernelAddressSpace());
        SchedConsumeDestroyQueue();
        SchedIdleWait();
        Schedule();
    }
}

/**
 * @brief Prepare the idle task's stack so the first switch to it starts the idle loop.
 */
static void SchedInitIdleTask(void)
{
    VirtAddr sp = (VirtAddr)idle_stack + sizeof(idle_stack);
    sp &= ~(VirtAddr)7U;

    idle_task.kernel_sp = (uint32_t *)ArchTaskKernelInit((void *)sp, IdleTask);
}

void SchedInit(void)
{
    for (uint32_t level = 0; level < SCHED_PRIORITY_LEVELS; level++)
        ListInit(&run_queues[level]);
    for (uint32_t level = 0; level < SLEEP_QUEUE_SIZE; level++)
        ListInit(&sleep_wheel[level]);

    assert(ArchTimerFreq() >= 250);

    slot_shift = (uint32_t)(63 - __builtin_clzll(ArchTimerFreq() / 250));
    wheel_now_slot = ArchTimerNow() >> slot_shift;
    current_task = NULL;
    on_idle_stack = false;
    SchedInitIdleTask();
    ArchTimerSetHandler(SchedTimerExpired);
}

/**
 * @brief Put a ready task on the run queue of its priority.
 *
 * Does nothing for a task in a frozen space, a suspended task, or one already queued. May request
 * a reschedule if the task outranks the running one.
 *
 * @param[in] t The task.
 * @param[in] front Whether to put it at the front of its queue instead of the back.
 */
static void SchedEnqueue(TaskObject *t, bool front)
{
    if (!t)
        return;
    if (t->owner && t->owner->frozen)
        return;
    if (t->suspended) return;
    if (t->node.next || t->node.prev)
        return;

    uint32_t priority = t->priority;
    if (priority >= SCHED_PRIORITY_LEVELS)
        priority = SCHED_PRIO_DEFAULT;

    t->queued_prio = (uint8_t)priority;
    if (front)
        ListAddHead(&t->node, &run_queues[priority].node);
    else
        ListAddTail(&t->node, &run_queues[priority].node);
    ready_mask |= (1U << priority);

    if (current_task && t->priority > current_task->priority) {
        do_resched = 1;
    }

    if (current_task && !in_tick && t != current_task && !slice_armed &&
        t->priority >= current_task->priority)
        SchedArmTimer();
}

void SchedAdd(TaskObject *t) { SchedEnqueue(t, false); }

void SchedAddFront(TaskObject *t) { SchedEnqueue(t, true); }

void SchedQueueDestroyTask(TaskObject *t)
{
    if (!t)
        return;
    if (t->destroy_node.next || t->destroy_node.prev) {
        return;
    }
    ListAddTail(&t->destroy_node, &task_destroy_queue.node);
}

/**
 * @brief Destroy every queued task except the running one, which stays queued.
 */
static void SchedConsumeDestroyQueue(void)
{
    ListHead deferred = LIST_HEAD_INIT(deferred);

    while (!ListIsEmpty(&task_destroy_queue)) {
        ListNode *node = ListPopFront(&task_destroy_queue);
        if (!node)
            break;
        TaskObject *t = container_of(node, TaskObject, destroy_node);

        if (t == current_task) {
            ListAddTail(&t->destroy_node, &deferred.node);
            continue;
        }

#ifdef CONFIG_ZUZU_BENCH
        uint32_t bench_start = BENCH_BEGIN();
#endif
        TaskDestroy(t);
#ifdef CONFIG_ZUZU_BENCH
        BENCH_END(g_bench_reap_task, bench_start);
#endif
    }

    while (!ListIsEmpty(&deferred)) {
        ListNode *node = ListPopFront(&deferred);
        if (!node)
            break;
        TaskObject *t = container_of(node, TaskObject, destroy_node);
        ListAddTail(&t->destroy_node, &task_destroy_queue.node);
    }
}

/**
 * @brief Check whether anything is ready to run or a reschedule was requested.
 *
 * @return true if the idle loop should stop waiting.
 */
static bool SchedIsWorkPending(void)
{
    if (do_resched)
        return true;

    for (uint32_t level = 0; level < SCHED_PRIORITY_LEVELS; level++) {
        if (!ListIsEmpty(&run_queues[level]))
            return true;
    }

    return false;
}

/**
 * @brief Find the earliest deadline among the tasks in a sleep wheel slot.
 *
 * @param[in] slot The slot.
 * @return The earliest deadline, or UINT64_MAX if the slot is empty.
 */
static uint64_t WheelBucketMin(uint32_t slot)
{
    uint64_t min = UINT64_MAX;
    ListHead *bucket = &sleep_wheel[slot];
    for (ListNode *n = bucket->node.next; n != &bucket->node; n = n->next) {
        TaskObject *t = container_of(n, TaskObject, timeout_node);
        if (t->wake_deadline < min)
            min = t->wake_deadline;
    }
    return min;
}

void SchedRemoveSleepQueue(TaskObject *t)
{
    if (t->sleep_slot < 0)
        return;
    if (t->timeout_node.prev && t->timeout_node.next)
        ListRemove(&t->timeout_node);
    uint32_t slot = (uint32_t)t->sleep_slot;
    if (ListIsEmpty(&sleep_wheel[slot]))
        BitmapClr(wheel_occ, slot);
    else if (t->wake_deadline == wheel_min[slot])
        wheel_min[slot] = WheelBucketMin(slot);
    t->sleep_slot = -1;
}

void SchedInsertSleepQueue(TaskObject *t)
{
    uint64_t abs_slot = t->wake_deadline >> slot_shift;
    if (abs_slot < wheel_now_slot)
        abs_slot = wheel_now_slot;
    if ((abs_slot - wheel_now_slot) >= SLEEP_QUEUE_SIZE) {
        abs_slot = wheel_now_slot + SLEEP_QUEUE_SIZE - 1;
    }
    uint32_t slot = (uint32_t)(abs_slot % SLEEP_QUEUE_SIZE);
    if (ListIsEmpty(&sleep_wheel[slot]) || t->wake_deadline < wheel_min[slot])
        wheel_min[slot] = t->wake_deadline;
    ListAddTail(&t->timeout_node, &sleep_wheel[slot].node);
    BitmapSet(wheel_occ, slot);
    t->sleep_slot = (int16_t)slot;
    SchedArmTimer();
}

/**
 * @brief Wake a task whose timeout expired, returning ERR_TIMEOUT from its syscall.
 *
 * A caller waiting on a call has the call withdrawn first.
 *
 * @param[in] t The task.
 */
static void SchedWakeTimedOut(TaskObject *t)
{
    t->sleep_slot = -1;
    if (t->ipc_state == IPC_WAITING) {
        TaskCancelCall(t);
        ArchSetInFrame(t->trap_frame, 1, 0);
        ArchSetInFrame(t->trap_frame, 3, (Register)-1);
    }
    ArchSetInFrame(t->trap_frame, 0, ERR_TIMEOUT);
    SchedUnblock(t);
    SchedAdd(t);
}

/**
 * @brief Wake every sleeper whose deadline has passed.
 *
 * Handles the slots time has moved past, then the tasks in the current slot that are already due.
 * Tasks whose deadline lies beyond the wheel are put back.
 */
static void SchedWakeSleepers(void)
{
    uint64_t now_slot = ArchTimerNow() >> slot_shift;
    if (now_slot - wheel_now_slot > SLEEP_QUEUE_SIZE)
        wheel_now_slot = now_slot - SLEEP_QUEUE_SIZE;

    for (; wheel_now_slot < now_slot; wheel_now_slot++) {
        uint32_t slot = (uint32_t)(wheel_now_slot % SLEEP_QUEUE_SIZE);
        ListHead *bucket = &sleep_wheel[slot];

        for (;;) {
            ListNode *node = ListPopFront(bucket);
            if (!node)
                break;
            TaskObject *t = container_of(node, TaskObject, timeout_node);
            t->sleep_slot = -1;
            if ((t->wake_deadline >> slot_shift) > wheel_now_slot) {
                SchedInsertSleepQueue(t);
                continue;
            }

            SchedWakeTimedOut(t);
        }

        BitmapClr(wheel_occ, slot);
    }

    uint64_t now = ArchTimerNow();
    uint32_t cur = (uint32_t)(wheel_now_slot % SLEEP_QUEUE_SIZE);
    ListHead *bucket = &sleep_wheel[cur];
    ListNode *node = bucket->node.next;
    while (node != &bucket->node) {
        ListNode *next = node->next;
        TaskObject *t = container_of(node, TaskObject, timeout_node);
        if (t->wake_deadline <= now) {
            ListRemove(node);
            SchedWakeTimedOut(t);
        }
        node = next;
    }
    if (ListIsEmpty(bucket))
        BitmapClr(wheel_occ, cur);
    else
        wheel_min[cur] = WheelBucketMin(cur);
}

/**
 * @brief Halt the CPU until an interrupt makes some task ready or requests a reschedule.
 */
static void SchedIdleWait(void)
{
    for (;;) {
        ArchGlobalIrqDisable();

        if (SchedIsWorkPending()) {
            if (do_resched)
                do_resched = 0;
            return;
        }

        ArchCpuIdle();
        ArchGlobalIrqEnable();
        ArchGlobalIrqDisable();

        if (SchedIsWorkPending()) {
            if (do_resched)
                do_resched = 0;
            return;
        }
    }
}

/**
 * @brief Reap dead tasks and wake expired sleepers; run on every reschedule.
 */
static void SchedDoHousekeeping(void)
{
    SchedConsumeDestroyQueue();
    SchedWakeSleepers();
}

/**
 * @brief Take the first task of the highest non-empty priority level off its run queue.
 *
 * @return The task, or the idle task when nothing is ready.
 */
static TaskObject *SchedPickNext(void)
{
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--) {
        if (ready_mask & (1U << level)) {
            if (unlikely(ListIsEmpty(&run_queues[level]))) {
                ready_mask &= ~(1U << level);
#ifdef DEBUG
                panic("ready_mask bit %d set on an empty run queue", level);
#endif
                continue;
            }
            ListNode *next_node = ListPopFront(&run_queues[level]);
            if (ListIsEmpty(&run_queues[level]))
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

void __hot SchedSwitchNext(TaskObject *next)
{
    TaskObject *prev = current_task;

    if (unlikely(next == &idle_task)) {
        bool from_idle = (prev == NULL && on_idle_stack);
        current_task = NULL;
        SchedArmTimer();
        if (from_idle) {
            return;
        }
        ContextSwitch(prev, &idle_task);
        return;
    }

    current_task = next;
    current_task->state = TASK_STATE_RUNNING;
    on_idle_stack = false;

    uint64_t slice = current_task->slice_remaining
                         ? current_task->slice_remaining
                         : ClockUsToCounts(current_task->time_slice_us);
    current_task->slice_deadline = ArchTimerNow() + slice;
    current_task->slice_remaining = 0;
    SchedArmTimer();

    if (unlikely(next == prev))
        return;

    if (unlikely(current_task == fpu_owner)) {
        if (!fpu_access_enabled) {
            ArchFpuEnableAccess();
            fpu_access_enabled = true;
        }
    } else {
        if (fpu_access_enabled) {
            ArchFpuTrapDisable();
            fpu_access_enabled = false;
        }
    }

    SpaceObject *prev_proc = prev ? prev->owner : NULL;
    if (unlikely(current_task->owner->as &&
                 (!prev_proc || prev_proc->as != current_task->owner->as))) {
        VmmActivateAddressSpace(current_task->owner->as);
    }
    ArchSetTlsPointer(current_task);
    ContextSwitch(prev, current_task);
}

/** @brief Smallest interval the timer is armed for, so a deadline never lands in the past. */
#define MIN_TIMER_SLACK (ArchTimerFreq() / 500000u)

/**
 * @brief Find the first occupied sleep wheel slot at or after the current time.
 *
 * @return The distance in slots, or SLEEP_QUEUE_SIZE if the wheel is empty.
 */
static uint32_t WheelScanFromNow(void)
{
    uint32_t start = (uint32_t)(wheel_now_slot % SLEEP_QUEUE_SIZE);
    for (uint32_t i = 0; i < SLEEP_QUEUE_SIZE; i++) {
        uint32_t slot = start + i;
        if (slot >= SLEEP_QUEUE_SIZE)
            slot -= SLEEP_QUEUE_SIZE;
        if (wheel_occ[slot >> 5] & (1U << (slot & 31)))
            return i;
    }
    return SLEEP_QUEUE_SIZE;
}

/**
 * @brief Program the timer for the next thing that needs it: a sleeper's deadline or the end of
 * the running task's slice. Turns the timer off when neither exists.
 */
static void SchedArmTimer(void)
{
    uint64_t now = ArchTimerNow();
    uint64_t deadline = UINT64_MAX;

    slice_armed = current_task && SchedAnyCpuTakers(current_task);

    uint32_t k = WheelScanFromNow();
    if (k < SLEEP_QUEUE_SIZE) {
        uint32_t slot = (uint32_t)((wheel_now_slot + k) % SLEEP_QUEUE_SIZE);
        uint64_t slot_end = (wheel_now_slot + k + 1) << slot_shift;
        deadline = wheel_min[slot] < slot_end ? wheel_min[slot] : slot_end;
    }

    if (slice_armed && current_task->slice_deadline < deadline)
        deadline = current_task->slice_deadline;

    if (deadline == UINT64_MAX) {
        
        ArchTimerDisable();
        return;
    }
    if (deadline <= now + MIN_TIMER_SLACK)
        deadline = now + MIN_TIMER_SLACK;

    ArchTimerSetDeadline(deadline);
}

void SchedBlockOn(ListHead *queue, Duration timeout)
{
    if (TIMEOUT_POLL == timeout) {
        ArchSetInFrame(current_task->trap_frame, 0, ERR_TIMEOUT);
        return;
    }

    current_task->wait_slot.owner = current_task;
    SchedWaitQueueAdd(queue, &current_task->wait_slot);

    current_task->state = TASK_STATE_BLOCKED;

    if (TIMEOUT_INFINITE != timeout) {
        current_task->wake_deadline = ArchDeadlineFromMs(timeout);
        SchedInsertSleepQueue(current_task);
    } else {
        current_task->wake_deadline = 0;
    }

    Schedule();
}

void SchedUnblock(TaskObject *t)
{
    if (t->wait_slot.node.next)
        ListRemove(&t->wait_slot.node);
    SchedRemoveSleepQueue(t);
    t->wake_deadline = 0;
    t->ipc_state = IPC_NONE;
    t->blocked_port = NULL;
    t->state = TASK_STATE_READY;
}

void __hot Schedule(void)
{
    if (current_task != NULL && current_task->state == TASK_STATE_RUNNING) {
        current_task->state = TASK_STATE_READY;

        uint64_t now = ArchTimerNow();
        bool preempted =
            now < current_task->slice_deadline && (ready_mask >> (current_task->priority + 1)) != 0;

        if (preempted) {
            current_task->slice_remaining = current_task->slice_deadline - now;
            SchedAddFront(current_task);
        } else {
            SchedAdd(current_task);
        }
    }

    SchedDoHousekeeping();

    TaskObject *next = SchedPickNext();
    SchedSwitchNext(next);
}

size_t SchedGetReadyQueue(TaskObject **out, size_t max_out)
{
    size_t total = 0;
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--) {
        ListNode *node = run_queues[level].node.next;

        while (node != &run_queues[level].node) {
            if (out && total < max_out) {
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
    ListRemove(&t->node);
    if (ListIsEmpty(&run_queues[priority]))
        ready_mask &= ~(1U << priority);
}

size_t SchedGetSleepers(TaskObject **out, size_t max_out)
{
    size_t total = 0;
    for (uint32_t s = 0; s < SLEEP_QUEUE_SIZE; s++) {
        ListNode *node = sleep_wheel[s].node.next;
        while (node != &sleep_wheel[s].node) {
            if (out && total < max_out)
                out[total] = container_of(node, TaskObject, timeout_node);
            total++;
            node = node->next;
        }
    }
    return total;
}

void SchedSetReschedFlag(void) { do_resched = 1; }

void SchedTimerExpired(void)
{
    in_tick = true;
    SchedWakeSleepers();
    in_tick = false;

    if (current_task && SchedAnyCpuTakers(current_task) &&
        ArchTimerNow() >= current_task->slice_deadline)
        do_resched = 1;
    SchedArmTimer();
}

void SchedSetEffective(TaskObject *t, Prio prio)
{
    bool on_run = t->state == TASK_STATE_READY && t->node.next;
    bool on_wait = t->wait_slot.node.next != NULL;
    bool on_senders = t->state == TASK_STATE_BLOCKED && t->node.next && t->blocked_port;

    if (on_run)
        SchedRemoveRunQueue(t);
    if (on_wait)
        ListRemove(&t->wait_slot.node);
    if (on_senders)
        ListRemove(&t->node);

    t->priority = prio;

    if (on_run)
        SchedAdd(t);
    if (on_wait)
        SchedWaitQueueAdd(t->wait_slot.queue, &t->wait_slot);
    if (on_senders)
        SchedWaitQueueAddTask(&t->blocked_port->sender_queue, t);

    if (t == current_task && SchedAnyCpuTakers(current_task))
        SchedSetReschedFlag();
}

void SchedWaitQueueAdd(ListHead *q, WaitSlot *slot)
{
    ListNode *pos = q->node.prev;
    while (pos != &q->node &&
           container_of(pos, WaitSlot, node)->owner->priority < slot->owner->priority)
        pos = pos->prev;
    slot->queue = q;
    ListAddHead(&slot->node, pos);
}

void SchedWaitQueueAddTask(ListHead *q, TaskObject *t)
{
    ListNode *pos = q->node.prev;
    while (pos != &q->node &&
           container_of(pos, TaskObject, node)->priority < t->priority)
        pos = pos->prev;
    ListAddHead(&t->node, pos);
}
