#ifndef KERNEL_SCHED_SCHED_H
#define KERNEL_SCHED_SCHED_H

#include "kernel/task/task.h"
#include <stdbool.h>
#include <stddef.h>

#define SCHED_PRIORITY_LEVELS 8
#define SCHED_PRIO_DEFAULT 1

extern void __attribute__((hot)) ContextSwitch(TaskObject *prev, TaskObject *next);

extern TaskObject *current_task;
extern bool fpu_access_enabled;

extern TaskObject *fpu_owner;

void SchedInit(void);
void SchedAdd(TaskObject *t);
void SchedAddFront(TaskObject *t);
void SchedQueueDestroyTask(TaskObject *t);
void __hot Schedule(void);
void SchedTimerExpired(void);
void SchedRemoveSleepQueue(TaskObject *t);
void SchedSetReschedFlag(void);
void SchedRemoveRunQueue(TaskObject *t);
void SchedInsertSleepQueue(TaskObject *t);
size_t SchedGetReadyQueue(TaskObject **out, size_t max_out);
size_t SchedGetSleepers(TaskObject **out, size_t max_out);
void SchedBlockOn(ListHead *queue, Duration timeout);
void SchedUnblock(TaskObject *t);
void SchedSetEffective(TaskObject *t, Prio prio);

bool SchedAnyCpuTakers(const TaskObject *t);
void SchedSwitchNext(TaskObject *next);
void SchedWaitQueueAdd(ListHead *q, WaitSlot *slot);
void SchedWaitQueueAddTask(ListHead *q, TaskObject *t);

extern volatile bool do_resched;

#endif
