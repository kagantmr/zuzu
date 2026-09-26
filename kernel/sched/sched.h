#ifndef KERNEL_SCHED_SCHED_H
#define KERNEL_SCHED_SCHED_H

#include "kernel/task/task.h"
#include <stdbool.h>
#include <stddef.h>

#define SCHED_PRIORITY_LEVELS 8
#define SCHED_PRIO_DEFAULT 1

extern void __attribute__((hot)) ContextSwitch(TaskObject *prev, TaskObject *next);

extern TaskObject *current_task;
extern bool fpu_access_enabled;;

// Thread whose registers currently live in the FPU hardware, or NULL if none.
// Cleared by thread_destroy() when the owning thread is freed. See
// arch/include/arch/fpu.h for the lazy-switch contract.
extern TaskObject *fpu_owner;

void SchedInit(void);
void SchedAdd(TaskObject *t);
void SchedQueueDestroyProcess(SpaceObject *p);
void SchedQueueDestroyThread(TaskObject *t);
void SchedConsumeDestroyQueue(void);
void SchedReap(void);
void SchedIdleWait(void);
void __hot Schedule(void);
void SchedSetReschedFlag(void);
void SchedRemoveSleepQueue(TaskObject *t);
void SchedInsertSleepQueue(TaskObject *t);
size_t SchedGetReadyQueue(TaskObject **out, size_t max_out);
size_t SchedGetSleepers(TaskObject **out, size_t max_out);
void SchedBlockOn(ListHead *queue, Duration timeout);
void SchedUnblock(TaskObject *t, WakeReason reason);

// Direct-switch support for callers (e.g. IPC handoff) that want to switch
// straight to a specific thread instead of going through sched_add()+
// schedule(). See kernel/sched/sched.c for the state-ownership contract on
// switch_to_thread and the priority argument for sched_has_ready_at_or_above.
bool SchedAnyCpuTakers(const TaskObject *t);
void SchedSwitchNext(TaskObject *next);

extern volatile uint8_t do_resched;

#endif
