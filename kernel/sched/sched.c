#include <arch/context.h>
#include "sched.h"
#include "kernel/proc/process.h"
#include <list.h>

#include "kernel/syscall/syscall.h"
#include <arch/thread.h>

#include <arch/cpu.h>
#include "kernel/mm/vmm.h"
#include "kernel/mm/alloc.h"
#include "kernel/time/tick.h"
#include <mem.h>

static inline uint32_t thread_priority(const thread_t *t)
{
	if (!t)
		return 0;

	return t->priority;
}

static list_head_t destroy_queue = LIST_HEAD_INIT(destroy_queue);
list_head_t sleep_queue = LIST_HEAD_INIT(sleep_queue);
static list_head_t thread_destroy_queue = LIST_HEAD_INIT(thread_destroy_queue);
thread_t *current_thread;

volatile uint8_t do_resched = 0; // needs spinlock guard on SMP

static thread_t idle_thread;  // only kernel_sp is used
static uint8_t idle_stack[4096] __attribute__((aligned(8)));
static bool on_idle_stack;

static list_head_t run_queues[SCHED_PRIORITY_LEVELS];

#define LOG_FMT(fmt) "(sched) " fmt
#include "core/log.h"

static void sched_idle_trampoline(void) __attribute__((noreturn));

static void sched_idle_trampoline(void)
{
    on_idle_stack = true;
    for (;;) {
        /* Ensure we're running on the kernel address space while reaping
         * deferred destroys so we never free the active user address space. */
        vmm_activate(vmm_get_kernel_as());
        sched_reap();
        sched_idle_wait();
        schedule();
    }
}

static void sched_init_idle_context(void)
{
    uintptr_t sp = (uintptr_t)idle_stack + sizeof(idle_stack);
    sp &= ~(uintptr_t)7u;

    idle_thread.kernel_sp =
        (uint32_t *)arch_thread_kernel_init((void *)sp, sched_idle_trampoline);
    idle_thread.state = RUNNING;
}

void sched_init() {
    for (uint32_t level = 0; level < SCHED_PRIORITY_LEVELS; level++)
        list_init(&run_queues[level]);
    list_init(&destroy_queue);
    list_init(&sleep_queue);
    current_thread = NULL;
    on_idle_stack = false;
    sched_init_idle_context();
}
void sched_add(thread_t *t) {
    if (!t)
        return;

    if (t->node.next||t->node.prev) return; // double enqueue guard

    uint32_t priority = thread_priority(t);
    if (priority >= SCHED_PRIORITY_LEVELS)
        priority = SCHED_PRIORITY_LEVELS - 1;

    list_add_tail(&t->node, &run_queues[priority].node);

    if (current_thread && t->priority < current_thread->priority) {
        do_resched = 1;
    }
}

void sched_defer_destroy(process_t *p) {
    list_add_tail(&p->destroy_node, &destroy_queue.node);
}

void sched_defer_destroy_thread(thread_t *t) {
    if (!t) return;
    /* Guard against double-enqueue: if node is already linked, skip. */
    if (t->destroy_node.next || t->destroy_node.prev) {
        KDEBUG("sched_defer_destroy_thread: tid=%u already queued", t->tid);
        return;
    }
    list_add_tail(&t->destroy_node, &thread_destroy_queue.node);
}

void sched_reap_thread_destroys(void) {
    list_head_t deferred = LIST_HEAD_INIT(deferred);

    while (!list_empty(&thread_destroy_queue)) {
        list_node_t *node = list_pop_front(&thread_destroy_queue);
        thread_t *t = container_of(node, thread_t, destroy_node);

        if (t == current_thread) {
            list_add_tail(&t->destroy_node, &deferred.node);
            continue;
        }

        thread_destroy(t);
    }

    while (!list_empty(&deferred)) {
        list_node_t *node = list_pop_front(&deferred);
        thread_t *t = container_of(node, thread_t, destroy_node);
        list_add_tail(&t->destroy_node, &thread_destroy_queue.node);
    }
}

void sched_reap(void) {
    /* Removed noisy debug logging to avoid flooding the console. */
    while (!list_empty(&destroy_queue)) {
        list_node_t *node = list_pop_front(&destroy_queue);
        process_t *p = container_of(node, process_t, destroy_node);
        process_destroy(p);
    }
    sched_reap_thread_destroys();
}

static bool sched_work_pending(void)
{
    if (do_resched || !list_empty(&destroy_queue))
        return true;

    for (uint32_t level = 0; level < SCHED_PRIORITY_LEVELS; level++) {
        if (!list_empty(&run_queues[level]))
            return true;
    }

    return false;
}

void sleep_queue_insert(thread_t *t) {
    list_node_t *curr;
    list_for_each(curr, &sleep_queue.node) {
        thread_t *s = container_of(curr, thread_t, timeout_node);
        if (t->wake_tick < s->wake_tick) {
            list_insert_before(&t->timeout_node, curr);
            return;
        }
    }
    list_add_tail(&t->timeout_node, &sleep_queue.node);
}

static void sched_wake_sleepers(void) {
    uint64_t now = get_ticks();
    while (!list_empty(&sleep_queue)) {
        list_node_t *head = sleep_queue.node.next;
        thread_t *t = container_of(head, thread_t, timeout_node);
        if (t->wake_tick > now) break;
        list_remove(&t->timeout_node);
        if (t->ipc_state == IPC_RECEIVER || t->ipc_state == IPC_SENDER) {
            if (t->ipc_state == IPC_SENDER) {
                if (t->node.prev && t->node.next)
                    list_remove(&t->node);
            } else {
                if (t->ep_wait_slot.node.prev && t->ep_wait_slot.node.next)
                    list_remove(&t->ep_wait_slot.node);
            }
            t->ipc_state = IPC_NONE;
            t->blocked_endpoint = NULL;
            t->wake_reason = WAKE_TIMEOUT;
            (*arch_reg(t->trap_frame, 0)) = ERR_TIMEOUT;
            t->state = READY;
            sched_add(t);
        } else {
            t->wake_reason = WAKE_TIMEOUT;
            if (t->trap_frame)
                (*arch_reg(t->trap_frame, 0)) = ERR_TIMEOUT;
            thread_recvany_clear_waits(t);
            thread_recvany_clear_ep_waits(t);
            if (t->ntfn_wait_slot.node.prev && t->ntfn_wait_slot.node.next)
                list_remove(&t->ntfn_wait_slot.node);
            t->state = READY;
            t->wake_tick = 0;
            sched_add(t);
        }
    }
}

void sched_idle_wait(void)
{
    for (;;) {
        arch_global_irq_disable();

        if (sched_work_pending()) {
            if (do_resched)
                do_resched = 0;
            arch_global_irq_enable();
            return;
        }

        __asm__ volatile("wfi" ::: "memory");
        arch_global_irq_enable();

        if (sched_work_pending()) {
            if (do_resched)
                do_resched = 0;
            return;
        }
    }
}


void __attribute__((hot)) schedule() {
    thread_t *prev = current_thread;
    bool from_idle = (prev == NULL && on_idle_stack);

    sched_reap_thread_destroys();

    if (current_thread != NULL) {
        if (current_thread->state == RUNNING) {
            current_thread->state = READY;
            sched_add(current_thread);
        }
    }

    sched_wake_sleepers();

    list_head_t *selected_queue = NULL;
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--) {
        if (!list_empty(&run_queues[level])) {
            selected_queue = &run_queues[level];
            break;
        }
    }

    if (!selected_queue) {
        current_thread = NULL;
        current_thread = NULL;
        if (from_idle) {
            return;
        }
        context_switch(prev, &idle_thread);
        return;
    }

    list_node_t *next_node = list_pop_front(selected_queue);
    current_thread = container_of(next_node, thread_t, node);
    current_thread->state = RUNNING;
    current_thread->ticks_remaining = current_thread->time_slice;
    on_idle_stack = false;

    process_t *prev_proc = prev ? prev->owner_process : NULL;
    if (current_thread->owner_process->as && (!prev_proc || prev_proc->as != current_thread->owner_process->as)) {
        vmm_activate(current_thread->owner_process->as);
    }
    arch_set_thread_ptr(current_thread);
    //KTRACE("Switching to thread %d (process %d)", current_thread->tid, current_thread->owner_process->pid);
    context_switch(prev, current_thread);
}

size_t sched_ready_queue_snapshot(thread_t **out, size_t max_out) {
    size_t total = 0;
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--) {
        list_node_t *node = run_queues[level].node.next;

        while (node != &run_queues[level].node) {
            if (out && total < max_out) {
                out[total] = container_of(node, thread_t, node);
            }
            total++;
            node = node->next;
        }
    }

    return total;
}

void set_resched_flag(void) {
    // decrement current_thread's time slice and set do_resched if it expires
    if (current_thread) {
        if (current_thread->ticks_remaining > 0) {
            current_thread->ticks_remaining--;
        }
        if (current_thread->ticks_remaining == 0) {
            do_resched = 1;
        }
    } else {
        do_resched = 1; // idling
    }
}
