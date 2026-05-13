#ifndef ZUZU_THREAD_H
#define ZUZU_THREAD_H

#include <zuzu/types.h>
#include <list.h>
#include "kernel/ipc/endpoint.h"
#include "kernel/mm/vmm.h"
#include "arch/arm/include/context.h"

typedef struct process process_t;

typedef enum thread_state
{
    READY = 0, // ready to run, in run queue
    RUNNING,   // on CPU
    BLOCKED,   // waiting for IPC or timeout
    ZOMBIE,    // called quit()
    FROZEN,   // not runnable yet
} state_t;

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

typedef struct
{
    vaddr_t kernel_stack_top; // base of kernel stack for freeing (offset 0)
    exception_frame_t *trap_frame; // pointer to saved user registers for IPC and context switching (offset 4)
    tid_t tid; // thread ID (offset 8)
    uint32_t *kernel_sp; // current kernel stack pointer for context switching (offset 12 - CRITICAL: switch.S offset)
    int32_t exit_status;
    list_node_t node; // embedded, not pointers
    list_node_t process_node; // membership in owner process thread list
    list_node_t timeout_node;
    wake_reason_t wake_reason;
    tick_t wake_tick;
    state_t state;
    ipc_state_t ipc_state;
    endpoint_t *blocked_endpoint;
    reply_cap_t *pending_reply_cap;
    paddr_t ipc_buf_pa;
    size_t ipc_buf_xfer_len;
    uint32_t priority, time_slice, ticks_remaining;
    process_t *owner_process; // backpointer to owning process
    vaddr_t thread_info_va; 
} thread_t;

#ifdef __cplusplus
static_assert(offsetof(thread_t, kernel_sp) == 12,
              "switch.S expects process->kernel_sp at offset 12");
#else
_Static_assert(offsetof(thread_t, kernel_sp) == 12,
               "switch.S expects process->kernel_sp at offset 12");
#endif

void thread_destroy(thread_t *thread);
thread_t *thread_create(process_t *owner_process);
void thread_kill(thread_t *thread);
thread_t *thread_find_by_tid(tid_t tid);

#endif // ZUZU_THREAD_H