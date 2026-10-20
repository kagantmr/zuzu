/**
 * @file sched.h
 * @brief The scheduler is an integral part of any operating system, deciding who gets CPU time. zuzu uses a multi-level
 * round robin scheduler.
 */

#ifndef KERNEL_SCHED_SCHED_H
#define KERNEL_SCHED_SCHED_H

#include "kernel/task/task.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief How many priorities zuzu has.
 */
#define SCHED_PRIORITY_LEVELS 8

/**
 * @brief Default priority value given to any task.
 */
#define SCHED_PRIO_DEFAULT 1

/**
 * @brief External definition for the assembly routine that performs the context switch from one task
 * to the next.
 *
 * @param[in] prev The task being switched out, or NULL when leaving the idle context.
 * @param[in] next The task to switch to.
 */
extern void __hot ContextSwitch(TaskObject *prev, TaskObject *next);

/**
 * @brief External definition for the current task pointer.
 */
extern TaskObject *current_task;

/**
 * @brief External definition for the flag that checks if the FPU is enabled.
 */
extern bool fpu_access_enabled;

/**
 * @brief External definition for the current FPU owner.
 */
extern TaskObject *fpu_owner;

/**
 * @brief Initialize the scheduler subsystem. 
 */
void SchedInit(void);

/**
 * @brief Add task @p t to ready queue.
 * 
 * @param[in] t The task to add.
 */
void SchedAdd(TaskObject *t);

/**
 * @brief Add task @p t to the front of the ready queue.
 * 
 * @param[in] t The task to add.
 */
void SchedAddFront(TaskObject *t);

/**
 * @brief Queue task @p t for destruction.
 * 
 * @param[in] t The task to destroy.
 */
void SchedQueueDestroyTask(TaskObject *t);

/**
 * @brief Scheduler entry point. Reschedules tasks.
 */
void __hot Schedule(void);

/**
 * @brief Re-arm the timer and wake any tasks that met their deadline.
 */
void SchedTimerExpired(void);

/**
 * @brief Remove @p t from the sleep queue
 * 
 * @param[in] t The task to remove.
 */
void SchedRemoveSleepQueue(TaskObject *t);

/**
 * @brief Flag a reschedule necessary.
 */
void SchedSetReschedFlag(void);

/**
 * @brief Remove @p t from the run queue
 * 
 * @param[in] t The task to remove.
 */
void SchedRemoveRunQueue(TaskObject *t);

/**
 * @brief Add task @p t to sleep queue.
 * 
 * @param[in] t The task to add.
 */
void SchedInsertSleepQueue(TaskObject *t);


/**
 * @brief Copy out the tasks waiting on the run queues, highest priority first.
 *
 * @param[out] out Receives up to @p max_out tasks; may be NULL to only count.
 * @param[in] max_out Capacity of @p out.
 * @return The total number of ready tasks, which can exceed @p max_out.
 */
size_t SchedGetReadyQueue(TaskObject **out, size_t max_out);

/**
 * @brief Copy out the tasks that have a timeout pending.
 *
 * @param[out] out Receives up to @p max_out tasks; may be NULL to only count.
 * @param[in] max_out Capacity of @p out.
 * @return The total number of sleeping tasks, which can exceed @p max_out.
 */
size_t SchedGetSleepers(TaskObject **out, size_t max_out);

/**
 * @brief Block the running task on a wait queue, optionally with a timeout, and switch away.
 *
 * With TIMEOUT_POLL it returns ERR_TIMEOUT in the task's frame at once instead of blocking. When
 * the task is woken by the timeout its syscall returns ERR_TIMEOUT.
 *
 * @param[in] queue The queue to wait on; entries are kept in priority order.
 * @param[in] timeout Milliseconds to wait, or TIMEOUT_POLL / TIMEOUT_INFINITE.
 */
void SchedBlockOn(ListHead *queue, Duration timeout);

/**
 * @brief Take a blocked task off its wait queue and timeout and mark it ready.
 *
 * The task is not put on the run queue; the caller does that with SchedAdd().
 *
 * @param[in] t The task to wake.
 */
void SchedUnblock(TaskObject *t);

/**
 * @brief Change a task's effective priority and move it within whatever queue it is on.
 *
 * @param[in] t The task.
 * @param[in] prio The new effective priority.
 */
void SchedSetEffective(TaskObject *t, Prio prio);

/**
 * @brief Check whether any ready task has a priority at or above a task's.
 *
 * @param[in] t The task to compare against.
 * @return true if the run queues hold a task that should run before or alongside @p t.
 */
bool SchedAnyCpuTakers(const TaskObject *t);

/**
 * @brief Make a task the running one: start its time slice, switch address space and FPU access,
 * and context switch to it.
 *
 * @param[in] next The task to run; the idle task when nothing is ready.
 */
void SchedSwitchNext(TaskObject *next);

/**
 * @brief Insert a wait slot into a wait queue, behind the entries of equal or higher priority.
 *
 * @param[in] q The wait queue.
 * @param[in] slot The slot to insert; its owner's priority decides the position.
 */
void SchedWaitQueueAdd(ListHead *q, WaitSlot *slot);

/**
 * @brief Insert a task, linked through its node, into a queue behind entries of equal or higher
 * priority. Used for the senders queued on a port.
 *
 * @param[in] q The queue.
 * @param[in] t The task to insert.
 */
void SchedWaitQueueAddTask(ListHead *q, TaskObject *t);

/**
 * @brief Set when a higher priority task became ready or a time slice ended; the interrupt exit
 * path calls Schedule() when it is set.
 */
extern volatile bool do_resched;

#endif
