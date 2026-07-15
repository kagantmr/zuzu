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
    FROZEN,    // not runnable yet
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

#ifndef WAITANY_MAX_HANDLES
#define WAITANY_MAX_HANDLES 16u
#endif

typedef struct thread thread_t;

#define TCB_SLOT_NONE 0xFFu /* thread holds no TCB slot */

typedef struct thread_wait_slot
{
    list_node_t node;
    thread_t *owner;
    uint32_t index;
} thread_wait_slot_t;

struct thread
{
    vaddr_t kernel_stack_top; // base of kernel stack for freeing (offset 0)
    arch_regs_t *trap_frame;  // pointer to saved user registers for IPC and context switching (offset 4)
    tid_t tid;                // thread ID (offset 8)
    uint32_t *kernel_sp;      // current kernel stack pointer for context switching (offset 12 - CRITICAL: switch.S offset)
    int32_t exit_status;
    list_node_t node;         // embedded, not pointers
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
    thread_wait_slot_t waitany_wait_slots[WAITANY_MAX_HANDLES];
    notification_t *waitany_wait_ntfns[WAITANY_MAX_HANDLES];
    uint32_t waitany_wait_count;
    uint32_t waitany_wait_match_index;
    uint32_t waitany_wait_bits;
    bool waitany_active;
    thread_wait_slot_t ep_wait_slot;                               /* for msg_recv */
    thread_wait_slot_t waitany_ep_wait_slots[WAITANY_MAX_HANDLES]; /* for waitany endpoints */
    endpoint_t *waitany_wait_eps[WAITANY_MAX_HANDLES];
    uint32_t waitany_ep_wait_count;
    bool waitany_ep_wait_active;
    uint32_t waitany_ep_wait_match_index;
    waitany_result_t waitany_pending_result;
    uint32_t priority, time_slice, ticks_remaining;
    process_t *owner_process; // backpointer to owning process
    vaddr_t thread_info_va;
    uint8_t tcb_slot; // index into owner's TCB page, TCB_SLOT_NONE if unassigned
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

static inline void thread_waitany_clear_waits(thread_t *thread)
{
    if (!thread || !thread->waitany_active)
        return;

    for (uint32_t i = 0; i < thread->waitany_wait_count && i < WAITANY_MAX_HANDLES; i++)
    {
        list_node_t *node = &thread->waitany_wait_slots[i].node;
        if (node->prev && node->next)
            list_remove(node);
        thread->waitany_wait_ntfns[i] = NULL;
        node->prev = NULL;
        node->next = NULL;
    }

    thread->waitany_wait_count = 0;
    thread->waitany_wait_match_index = WAITANY_NO_MATCH;
    thread->waitany_wait_bits = 0;
    thread->waitany_active = false;
}

static inline void thread_waitany_clear_ep_waits(thread_t *thread)
{
    if (!thread || !thread->waitany_ep_wait_active)
        return;

    for (uint32_t i = 0; i < thread->waitany_ep_wait_count && i < WAITANY_MAX_HANDLES; i++)
    {
        list_node_t *node = &thread->waitany_ep_wait_slots[i].node;
        if (node->prev && node->next)
            list_remove(node);
        thread->waitany_wait_eps[i] = NULL;
        node->prev = NULL;
        node->next = NULL;
    }

    thread->waitany_ep_wait_count = 0;
    thread->waitany_ep_wait_match_index = WAITANY_NO_MATCH;
    thread->waitany_ep_wait_active = false;
}

#endif // ZUZU_THREAD_H