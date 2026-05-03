#ifndef KERNEL_PROC_PROCESS_H
#define KERNEL_PROC_PROCESS_H

#include <stddef.h>
#include <stdint.h>
#include <list.h>
#include "kernel/ipc/endpoint.h"
#include "kernel/mm/vmm.h"
#include "arch/arm/include/context.h"

#define MAX_PROCESSES 512

#define PROC_FLAG_INIT (1 << 0)   // PID 1, zombie reaper
#define PROC_FLAG_DEVMGR (1 << 1) // hardware authority

extern void process_entry_trampoline(void);

typedef enum process_state
{
    PROCESS_READY = 0, // ready to run, in run queue
    PROCESS_RUNNING,   // on CPU
    PROCESS_BLOCKED,   // waiting for IPC or timeout
    PROCESS_ZOMBIE,    // called quit()
    PROCESS_STOPPED,   // not runnable yet
} p_state_t;

typedef enum
{
    WAKE_NONE = 0, // not currently sleeping/waiting
    WAKE_IPC,      // woken by IPC partner
    WAKE_TIMEOUT,  // woken by timer
} wake_reason_t;

typedef enum ipc_state
{
    IPC_NONE = 0,
    IPC_SENDER,
    IPC_RECEIVER,
    IPC_WAITING,
} ipc_state_t;

typedef struct process
{
    uint32_t pid, parent_pid;
    p_state_t process_state;
    uint32_t *kernel_sp;
    uintptr_t kernel_stack_top; // base of kernel stack for freeing
    uint64_t wake_tick;
    uint32_t priority, time_slice, ticks_remaining;
    addrspace_t *as;
    list_node_t node; // embedded, not pointers
    list_node_t timeout_node;
    int32_t exit_status;
    uint32_t waiting_for;
    char name[32];           // PROCESS name
    uint32_t device_va_next; // initialized to USER_DEVICE_BASE in process_create
    uint32_t mmap_va_next;   // initialized to USER_MMAP_BASE in process_create
    exception_frame_t *trap_frame;
    list_head_t outstanding_replies;
    handle_vec_t handle_table;
    uintptr_t ipc_buf_pa;
    reply_cap_t *pending_reply_cap;
    uint32_t ipc_buf_xfer_len;
    ipc_state_t ipc_state;
    wake_reason_t wake_reason;
    endpoint_t *blocked_endpoint;
    uint32_t flags;
    list_head_t children;
    list_node_t sibling_node;
} process_t;

typedef struct cpu_context
{
    uint32_t r4, r5, r6, r7, r8, r9, r10, r11;
    uint32_t lr; // return address (or entry point for new process)
} cpu_context_t;

#ifdef __cplusplus
static_assert(offsetof(process_t, kernel_sp) == 12,
              "switch.S expects process->kernel_sp at offset 12");
#else
_Static_assert(offsetof(process_t, kernel_sp) == 12,
               "switch.S expects process->kernel_sp at offset 12");
#endif

void process_destroy(process_t *process);
process_t *process_find_by_pid(uint32_t pid);
process_t *process_create(const char *name);
void process_kill(process_t *p, int exit_status);
void process_set_parent(process_t *child, process_t *parent);
process_t *process_find_child_by_pid(process_t *parent, uint32_t pid);
process_t *process_find_zombie_child(process_t *parent);
void process_track_reply_cap(process_t *caller, process_t *holder,
                             uint32_t holder_slot, reply_cap_t *rc);
void process_untrack_reply_cap(reply_cap_t *rc);

#endif // KERNEL_PROC_PROCESS_H
