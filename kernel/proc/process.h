#ifndef KERNEL_PROC_PROCESS_H
#define KERNEL_PROC_PROCESS_H

#include <stdint.h>
#include <list.h>
#include "kernel/ipc/endpoint.h"
#include "kernel/mm/vmm.h"
#include "arch/arm/include/context.h"

#define MAX_PROCESSES 64

#define PROC_FLAG_INIT      (1 << 0)  // PID 1, zombie reaper
#define PROC_FLAG_DEVMGR    (1 << 1)  // hardware authority

extern void process_entry_trampoline(void);

typedef enum process_state {
    PROCESS_READY = 0,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE,
} p_state_t;

typedef enum ipc_state {
    IPC_NONE = 0,
    IPC_SENDER,
    IPC_RECEIVER,
    IPC_WAITING,
} ipc_state_t;

typedef struct process {
    uint32_t pid, parent_pid;
    p_state_t process_state;
    uint32_t *kernel_sp;
    uintptr_t kernel_stack_top;   // base of kernel stack for freeing
    uint32_t wake_tick;
    uint32_t priority, time_slice, ticks_remaining;
    addrspace_t *as;
    list_node_t node;  // embedded, not pointers
    int32_t exit_status;
    uint32_t waiting_for;
    char name[16]; // PROCESS name
    uint32_t device_va_next;  // initialized to 0x60000000 in process_create
    uint32_t mmap_va_next;  // initialized to 0x20000000 in process_create
    exception_frame_t *trap_frame;
    handle_vec_t handle_table;
    uintptr_t ipc_buf_pa;
    uint32_t  ipc_buf_xfer_len;
    ipc_state_t ipc_state;
    endpoint_t *blocked_endpoint;
    uint32_t flags; 
} process_t;

typedef struct cpu_context {
    uint32_t r4, r5, r6, r7, r8, r9, r10, r11;
    uint32_t lr;   // return address (or entry point for new process)
} cpu_context_t;

void process_destroy(process_t *process);
process_t *process_find_by_pid(uint32_t pid);
void process_kill(process_t *p, int exit_status);

#endif // KERNEL_PROC_PROCESS_H