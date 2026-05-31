#ifndef KERNEL_SCHED_SCHED_H
#define KERNEL_SCHED_SCHED_H

#include "kernel/proc/process.h"
#include <stddef.h>

#define SCHED_PRIORITY_LEVELS 8

extern void __attribute__((hot)) context_switch(thread_t *prev, thread_t *next);

extern thread_t *current_thread;

void sched_init();
void sched_add(thread_t *t);
void sched_defer_destroy(process_t *p);
void sched_defer_destroy_thread(thread_t *t);
void sched_reap_thread_destroys(void);
void sched_reap(void);
void sched_idle_wait(void);
void __attribute__((hot)) schedule();
void set_resched_flag(void);
void sleep_queue_insert(thread_t *t);
size_t sched_ready_queue_snapshot(thread_t **out, size_t max_out);

extern volatile uint8_t do_resched;

#endif
