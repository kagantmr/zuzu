#ifndef KERNEL_SCHED_SCHED_H
#define KERNEL_SCHED_SCHED_H

#include "kernel/proc/process.h"

extern void context_switch(process_t *prev, process_t *next);

void sched_init();
void sched_add(process_t *p);
void sched_defer_destroy(process_t *p);
void sched_reap_zombies(void);
void schedule();

#endif