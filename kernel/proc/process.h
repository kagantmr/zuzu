#ifndef KERNEL_PROC_PROCESS_H
#define KERNEL_PROC_PROCESS_H

#include <stdint.h>
#include "lib/list.h"

typedef enum process_state {
    PROCESS_READY = 0,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE,
} p_state_t;

typedef struct process {
    uint32_t pid, parent_pid;
    p_state_t process_state;
    uint32_t *kernel_sp;
    uint32_t ttbr0;
    uint32_t priority, time_slice, ticks_remaining;
    list_node_t node;  // fixed: embedded, not pointers
} process_t;

typedef struct cpu_context {
    uint32_t r4, r5, r6, r7, r8, r9, r10, r11;
    uint32_t lr;   // return address (or entry point for new process)
} cpu_context_t;

process_t *process_create(void (*entry)(void));
void process_destroy(process_t *process);
void process_print(const process_t *process);

#endif // KERNEL_PROC_PROCESS_H