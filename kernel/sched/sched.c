#include <arch/context.h>
#include "sched.h"
#include "kernel/proc/process.h"
#include <compiler.h>
#include <list.h>

#include "kernel/syscall/syscall.h"
#include <arch/thread.h>
#include <arch/fpu.h>

#include <arch/cpu.h>
#include "kernel/mm/vmm.h"
#include "kernel/mm/alloc.h"
#include "kernel/time/tick.h"

#include <string.h>

/* Leaf: one branch, one field read. Called from sched_has_ready_at_or_above
 * on every direct-handoff decision. */
static __always_inline uint32_t thread_priority(const thread_t *t)
{
	if (unlikely(!t))
		return 0;

	return t->priority;
}

static ListHead destroy_queue = LIST_HEAD_INIT(destroy_queue);
ListHead sleep_queue = LIST_HEAD_INIT(sleep_queue);
static ListHead thread_destroy_queue = LIST_HEAD_INIT(thread_destroy_queue);
thread_t *current_thread;

/* Diagnostic-only: lets low-level modules (e.g. PMM_TRACE) attribute an
 * allocation to a process without taking on a thread.h/process.h
 * dependency themselves. */
uint32_t current_pid_or_zero(void)
{
	return (current_thread && current_thread->owner_process) ? current_thread->owner_process->pid : 0;
}

thread_t *fpu_owner = NULL;

volatile uint8_t do_resched = 0; // needs spinlock guard on SMP

static thread_t idle_thread;  // only kernel_sp is used
static uint8_t idle_stack[4096] __attribute__((aligned(8)));
static bool on_idle_stack;

static ListHead run_queues[SCHED_PRIORITY_LEVELS];

// Bit `level` set iff run_queues[level] is non-empty. Kept in sync by the
// only two call sites that ever add to or remove from a run queue
// (sched_add, sched_pick_next), so anything that needs to know "is level L
// or higher occupied" can answer in O(1) instead of scanning list_empty()
// across every level.
_Static_assert(SCHED_PRIORITY_LEVELS <= 32, "ready_mask is a uint32_t");
static uint32_t ready_mask = 0;

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
    ready_mask |= (1u << priority);

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
        return;
    }
    list_add_tail(&t->destroy_node, &thread_destroy_queue.node);
}

void sched_reap_thread_destroys(void) {
    ListHead deferred = LIST_HEAD_INIT(deferred);

    while (!list_empty(&thread_destroy_queue)) {
        ListNode *node = list_pop_front(&thread_destroy_queue);
        thread_t *t = container_of(node, thread_t, destroy_node);

        if (t == current_thread) {
            list_add_tail(&t->destroy_node, &deferred.node);
            continue;
        }

        thread_destroy(t);
    }

    while (!list_empty(&deferred)) {
        ListNode *node = list_pop_front(&deferred);
        thread_t *t = container_of(node, thread_t, destroy_node);
        list_add_tail(&t->destroy_node, &thread_destroy_queue.node);
    }
}

void sched_reap(void) {
    /* Removed noisy debug logging to avoid flooding the console. */
    while (!list_empty(&destroy_queue)) {
        ListNode *node = list_pop_front(&destroy_queue);
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
    ListNode *curr;
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
        ListNode *head = sleep_queue.node.next;
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
            t->blocked_port = NULL;
            t->wake_reason = WAKE_TIMEOUT;
            (*arch_reg(t->trap_frame, 0)) = ERR_TIMEOUT;
            t->state = READY;
            sched_add(t);
        } else {
            t->wake_reason = WAKE_TIMEOUT;
            if (t->trap_frame)
                (*arch_reg(t->trap_frame, 0)) = ERR_TIMEOUT;
            thread_waitany_clear_waits(t);
            thread_waitany_clear_ep_waits(t);
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


/*
 * sched_housekeeping - generic bookkeeping that must happen before picking
 * a thread to run: reap threads whose destruction was deferred, then wake
 * any sleepers whose timeout has elapsed (moving them onto the run queues).
 *
 * Order matters: this must run after the outgoing thread (if any) has
 * already been re-added to its run queue, so that a sleeper waking up at
 * the same priority is queued *after* it (FIFO fairness) rather than
 * jumping the line. schedule() enforces that ordering by requeuing the
 * outgoing thread before calling this.
 */
static void sched_housekeeping(void) {
    sched_reap_thread_destroys();
    sched_wake_sleepers();
}

/*
 * sched_pick_next - pure selection: scan the priority run queues
 * highest-first and return the winning thread, or &idle_thread if every
 * queue is empty. Popping the winner off its run queue is the one
 * necessary side effect of "selecting" it; this never touches current_thread,
 * on_idle_stack, thread state, or performs any switching.
 */
static thread_t *sched_pick_next(void) {
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--) {
        if (ready_mask & (1u << level)) {
            ListNode *next_node = list_pop_front(&run_queues[level]);
            if (list_empty(&run_queues[level]))
                ready_mask &= ~(1u << level);
            return container_of(next_node, thread_t, node);
        }
    }
    return &idle_thread;
}

/*
 * sched_has_ready_at_or_above - true if some thread at t's priority level or
 * higher is already sitting in a run queue. Used by direct-switch callers
 * (e.g. the IPC handoff path) to decide whether it's safe to switch straight
 * to a newly-woken thread `t` without going through sched_add()+schedule():
 * it is, exactly when nothing at t's level or above is already waiting,
 * since that's the only case where the full scheduler would have picked `t`
 * next anyway (a same-level thread queued earlier would win on FIFO order;
 * a higher-level thread would win on priority).
 *
 * O(1) via ready_mask rather than scanning list_empty() per level -- this
 * sits on the IPC fast path, so its own cost has to stay negligible next to
 * whatever it's saving.
 */
bool __hot sched_has_ready_at_or_above(const thread_t *t) {
    uint32_t priority = thread_priority(t);
    if (unlikely(priority >= SCHED_PRIORITY_LEVELS))
        priority = SCHED_PRIORITY_LEVELS - 1;

    uint32_t at_or_above = ready_mask & ~((1u << priority) - 1u);
    return at_or_above != 0;
}

/* Called from schedule() (every voluntary reschedule) and directly from
 * SysMsgCall's/SysMsgLcall's direct-handoff path -- one of the hottest
 * functions in the kernel. */
void __hot switch_to_thread(thread_t *next) {
    thread_t *prev = current_thread;

    if (unlikely(next == &idle_thread)) {
        bool from_idle = (prev == NULL && on_idle_stack);
        current_thread = NULL;
        if (from_idle) {
            return;
        }
        context_switch(prev, &idle_thread);
        return;
    }

    current_thread = next;
    current_thread->state = RUNNING;
    on_idle_stack = false;

    if (unlikely(next == prev))
        return;

    /* CPACR must track fpu_owner exactly, not just get disabled on the way
     * out: if some other thread ran in between and this thread is still
     * fpu_owner, access was disabled for that thread and never re-enabled
     * on the way back, so the owner's next FPU instruction would trap with
     * fpu_owner already == current_thread -- the undef handler treats that
     * as a genuine bad opcode and kills the process instead of granting
     * access (see arch/arm/exceptions/exception.c). Most threads never
     * touch the FPU, so "this thread owns it" is the rare case. */
    if (unlikely(current_thread == fpu_owner)) {
        arch_fpu_trap_enable();
    } else {
        arch_fpu_trap_disable();
    }

    /* Benchmark-relevant common case: sender/receiver are two threads in
     * the same process (e.g. speedtest's echo-server pattern), so the
     * address space is already active and vmm_activate() can be skipped. */
    process_t *prev_proc = prev ? prev->owner_process : NULL;
    if (unlikely(current_thread->owner_process->as &&
		 (!prev_proc || prev_proc->as != current_thread->owner_process->as))) {
        vmm_activate(current_thread->owner_process->as);
    }
    arch_set_thread_ptr(current_thread);
    //KTRACE("Switching to thread %d (process %d)", current_thread->tid, current_thread->owner_process->pid);
    context_switch(prev, current_thread);
}

void __attribute__((hot)) schedule(void) {
    if (current_thread != NULL && current_thread->state == RUNNING) {
        current_thread->state = READY;
        sched_add(current_thread);
    }

    sched_housekeeping();

    thread_t *next = sched_pick_next();
    next->ticks_remaining = next->time_slice;
    switch_to_thread(next);
}

size_t sched_ready_queue_snapshot(thread_t **out, size_t max_out) {
    size_t total = 0;
    for (int level = SCHED_PRIORITY_LEVELS - 1; level >= 0; level--) {
        ListNode *node = run_queues[level].node.next;

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
