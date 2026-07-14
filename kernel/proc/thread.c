#include "thread.h"
#include "process.h"
#include "kernel/mm/alloc.h"
#include "kstack.h"
#include <mem.h>
#include <spinlock.h>

#define MAX_THREADS 1024

#define LOG_FMT(fmt) "(thread) " fmt
#include "core/log.h"

static tid_t next_tid = 1;
static thread_t *thread_table[MAX_THREADS];
static spinlock_t thread_table_lock = SPINLOCK_INIT;

static int thread_table_find_free_slot(void)
{
    tid_t start = next_tid % MAX_THREADS;
    tid_t slot = start;

    do {
        if (thread_table[slot] == NULL)
            return (int)slot;

        slot = (slot + 1) % MAX_THREADS;
    } while (slot != start);

    return -1;
}

tid_t thread_register(thread_t *thread)
{
    if (!thread)
        return 0;

    spin_lock(&thread_table_lock);

    int slot = thread_table_find_free_slot();
    if (slot < 0) {
        spin_unlock(&thread_table_lock);
        return 0;
    }

    thread->tid = next_tid++;
    thread_table[slot] = thread;

    KTRACE("thread register: tid=%u slot=%d owner_pid=%u owner_name=%s",
           thread->tid,
           slot,
           (thread->owner_process ? thread->owner_process->pid : 0),
           (thread->owner_process ? thread->owner_process->name : "<none>"));

    spin_unlock(&thread_table_lock);
    return thread->tid;
}

void thread_unregister(thread_t *thread)
{
    if (!thread || thread->tid == 0)
        return;

    spin_lock(&thread_table_lock);

    for (uint32_t slot = 0; slot < MAX_THREADS; slot++) {
        if (thread_table[slot] == thread) {
            thread_table[slot] = NULL;
            break;
        }
    }

    spin_unlock(&thread_table_lock);
}

void thread_kill(thread_t *thread)
{
    if (!thread)
        return;

    thread->state = ZOMBIE;
}

void thread_destroy(thread_t *thread) {
    if (!thread) return;
    thread_unregister(thread);
    // may already be removed by tquit, guard is safe
    if (thread->process_node.prev && thread->process_node.next)
        list_remove(&thread->process_node);
    process_t *owner = thread->owner_process;
    /* Release the TCB slot; scrub it so a reused slot never shows a
     * previous thread's tid/pid. tcb_page_pa == 0 means the page is
     * already gone (process teardown fail paths). */
    if (owner && thread->tcb_slot < TCB_MAX_SLOTS && owner->tcb_page_pa) {
        memset((void *)(PA_TO_VA(owner->tcb_page_pa) +
                        thread->tcb_slot * TCB_SLOT_SIZE),
               0, TCB_SLOT_SIZE);
        tcb_slot_free(owner, thread->tcb_slot);
    }
    if (owner && owner->thread == thread)
        owner->thread = NULL;
    if (thread->kernel_stack_top)
        kstack_free(thread->kernel_stack_top);
    kfree(thread);
}

thread_t *thread_create(process_t *owner_process)
{
    if (!owner_process)
        return NULL;

    thread_t *thread = kmalloc(sizeof(*thread));
    if (!thread)
        return NULL;

    memset(thread, 0, sizeof(*thread));

    thread->kernel_stack_top = kstack_alloc();
    if (!thread->kernel_stack_top) {
        kfree(thread);
        return NULL;
    }

    thread->tid = thread_register(thread);
    if (thread->tid == 0) {
        kstack_free(thread->kernel_stack_top);
        kfree(thread);
        return NULL;
    }

    thread->kernel_sp = NULL;
    thread->trap_frame = NULL;
    thread->owner_process = owner_process;
    thread->exit_status = 0;
    thread->node.next = NULL;
    thread->node.prev = NULL;
    thread->process_node.next = NULL;
    thread->process_node.prev = NULL;
    thread->timeout_node.next = NULL;
    thread->timeout_node.prev = NULL;
    thread->wake_reason = WAKE_NONE;
    thread->wake_tick = 0;
    thread->state = FROZEN;
    thread->ipc_state = IPC_NONE;
    thread->blocked_endpoint = NULL;
    thread->pending_reply_cap = NULL;
    thread->ipc_buf_pa = 0;
    thread->ipc_buf_xfer_len = 0;
    thread->priority = 1;
    thread->time_slice = 5;
    thread->ticks_remaining = thread->time_slice;
    thread->thread_info_va = 0;
    thread->tcb_slot = TCB_SLOT_NONE;

    list_add_tail(&thread->process_node, &owner_process->threads.node);

    if (!owner_process->thread)
        owner_process->thread = thread;

    KTRACE("thread create: tid=%u owner_pid=%u owner_name=%s state=%u kernel_stack_top=%p",
           thread->tid,
           owner_process->pid,
           owner_process->name,
           thread->state,
           (void *)thread->kernel_stack_top);

    return thread;
}

thread_t *thread_find_by_tid(tid_t tid)
{
    if (tid == 0)
        return NULL;

    spin_lock(&thread_table_lock);
    for (uint32_t slot = 0; slot < MAX_THREADS; slot++) {
        thread_t *thread = thread_table[slot];
        if (thread && thread->tid == tid) {
            spin_unlock(&thread_table_lock);
            return thread;
        }
    }
    spin_unlock(&thread_table_lock);

    return NULL;
}
