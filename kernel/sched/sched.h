#ifndef KERNEL_SCHED_SCHED_H
#define KERNEL_SCHED_SCHED_H

#include "kernel/proc/process.h"
#include <stdbool.h>
#include <stddef.h>

#define SCHED_PRIORITY_LEVELS 8
#define SCHED_PRIO_DEFAULT 1

extern void __attribute__((hot)) context_switch(Thread *prev, Thread *next);

extern Thread *current_thread;

// Thread whose registers currently live in the FPU hardware, or NULL if none.
// Cleared by thread_destroy() when the owning thread is freed. See
// arch/include/arch/fpu.h for the lazy-switch contract.
extern Thread *fpu_owner;

void SchedInit(void);
void SchedAdd(Thread *t);
void SchedQueueDestroyProcess(ProcessObj *p);
void SchedQueueDestroyThread(Thread *t);
void SchedConsumeDestroyQueue(void);
void SchedReap(void);
void SchedIdleWait(void);
void __attribute__((hot)) Schedule(void);
void SchedSetReschedFlag(void);
void SchedInsertSleepQueue(Thread *t);
size_t SchedGetReadyQueue(Thread **out, size_t max_out);

// Direct-switch support for callers (e.g. IPC handoff) that want to switch
// straight to a specific thread instead of going through sched_add()+
// schedule(). See kernel/sched/sched.c for the state-ownership contract on
// switch_to_thread and the priority argument for sched_has_ready_at_or_above.
bool SchedAnyCpuTakers(const Thread *t);
void SchedSwitchNext(Thread *next);

extern volatile uint8_t do_resched;

#endif
