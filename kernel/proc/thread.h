#ifndef ZUZU_THREAD_H
#define ZUZU_THREAD_H

#include <zuzu/types.h>
#include <list.h>
#include "kernel/ipc/port.h"
#include "kernel/mm/vmm.h"
#include <arch/regs.h>

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

#ifndef RECVANY_MAX_HANDLES
#define RECVANY_MAX_HANDLES 16u
#endif

typedef struct thread thread_t;

typedef struct thread_wait_slot
{
    list_node_t node;
    thread_t *owner;
    uint32_t index;
} thread_wait_slot_t;

struct thread
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
    list_node_t destroy_node;
    ipc_state_t ipc_state;
    endpoint_t *blocked_endpoint;
    reply_cap_t *pending_reply_cap;
    paddr_t ipc_buf_pa;
    size_t ipc_buf_xfer_len;
    thread_wait_slot_t ntfn_wait_slot;
    thread_wait_slot_t recvany_wait_slots[RECVANY_MAX_HANDLES];
    notification_t *recvany_wait_ntfns[RECVANY_MAX_HANDLES];
    uint32_t recvany_wait_count;
    uint32_t recvany_wait_match_index;
    uint32_t recvany_wait_bits;
    bool recvany_wait_active;
    thread_wait_slot_t ep_wait_slot;                               /* for proc_recv */
    thread_wait_slot_t recvany_ep_wait_slots[RECVANY_MAX_HANDLES]; /* for proc_recvany endpoints */
    endpoint_t *recvany_wait_eps[RECVANY_MAX_HANDLES];
    uint32_t recvany_ep_wait_count;
    bool recvany_ep_wait_active;
    uint32_t recvany_ep_wait_match_index;
    recvany_result_t recvany_pending_result;
    uint32_t priority, time_slice, ticks_remaining;
    process_t *owner_process; // backpointer to owning process
    vaddr_t thread_info_va; 
};

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

static inline void thread_recvany_clear_waits(thread_t *thread)
{
    if (!thread || !thread->recvany_wait_active)
        return;

    for (uint32_t i = 0; i < thread->recvany_wait_count && i < RECVANY_MAX_HANDLES; i++) {
        list_node_t *node = &thread->recvany_wait_slots[i].node;
        if (node->prev && node->next)
            list_remove(node);
        thread->recvany_wait_ntfns[i] = NULL;
        node->prev = NULL;
        node->next = NULL;
    }

    thread->recvany_wait_count = 0;
    thread->recvany_wait_match_index = RECVANY_NO_MATCH;
    thread->recvany_wait_bits = 0;
    thread->recvany_wait_active = false;
}

static inline void thread_recvany_clear_ep_waits(thread_t *thread)
{
    if (!thread || !thread->recvany_ep_wait_active)
        return;

    for (uint32_t i = 0; i < thread->recvany_ep_wait_count && i < RECVANY_MAX_HANDLES; i++) {
        list_node_t *node = &thread->recvany_ep_wait_slots[i].node;
        if (node->prev && node->next)
            list_remove(node);
        thread->recvany_wait_eps[i] = NULL;
        node->prev = NULL;
        node->next = NULL;
    }

    thread->recvany_ep_wait_count = 0;
    thread->recvany_ep_wait_match_index = RECVANY_NO_MATCH;
    thread->recvany_ep_wait_active = false;
}

#endif // ZUZU_THREAD_H