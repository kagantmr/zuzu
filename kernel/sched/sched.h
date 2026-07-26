#ifndef KERNEL_SCHED_SCHED_H
#define KERNEL_SCHED_SCHED_H

#include "kernel/proc/process.h"
#include <stddef.h>
#include <stdbool.h>

#define SCHED_PRIORITY_LEVELS 8

extern void __attribute__((hot)) context_switch(thread_t *prev, thread_t *next);

extern thread_t *current_thread;

// Thread whose registers currently live in the FPU hardware, or NULL if none.
// Cleared by thread_destroy() when the owning thread is freed. See
// arch/include/arch/fpu.h for the lazy-switch contract.
extern thread_t *fpu_owner;

void sched_init(void);
void sched_add(thread_t *t);
void sched_defer_destroy(process_t *p);
void sched_defer_destroy_thread(thread_t *t);
void sched_reap_thread_destroys(void);
void sched_reap(void);
void sched_idle_wait(void);
void __attribute__((hot)) schedule(void);
void set_resched_flag(void);
void sleep_queue_insert(thread_t *t);
size_t sched_ready_queue_snapshot(thread_t **out, size_t max_out);

// Direct-switch support for callers (e.g. IPC handoff) that want to switch
// straight to a specific thread instead of going through sched_add()+
// schedule(). See kernel/sched/sched.c for the state-ownership contract on
// switch_to_thread and the priority argument for sched_has_ready_at_or_above.
bool sched_has_ready_at_or_above(const thread_t *t);
void switch_to_thread(thread_t *next);

extern volatile uint8_t do_resched;

#endif
