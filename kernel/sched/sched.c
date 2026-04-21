#include "sched.h"
#include "kernel/proc/process.h"
#include <list.h>

#include "kernel/mm/vmm.h"
#include "kernel/mm/alloc.h"
#include "kernel/time/tick.h"

static list_head_t run_queue = LIST_HEAD_INIT(run_queue); 
static list_head_t destroy_queue = LIST_HEAD_INIT(destroy_queue);
list_head_t sleep_queue = LIST_HEAD_INIT(sleep_queue);
process_t *current_process;

volatile uint8_t do_resched = 0;

static process_t idle_proc;  // only kernel_sp is used

#define LOG_FMT(fmt) "(sched) " fmt
#include "core/log.h"


void sched_init() {
    list_init(&run_queue);
    list_init(&destroy_queue);
    list_init(&sleep_queue);
    current_process = NULL;
}
void sched_add(process_t *p) {
    list_add_tail(&p->node, &run_queue.node);
}

void sched_defer_destroy(process_t *p) {
    list_add_tail(&p->node, &destroy_queue.node);
}

void sched_reap(void) {
    while (!list_empty(&destroy_queue)) {
        list_node_t *node = list_pop_front(&destroy_queue);
        process_t *p = container_of(node, process_t, node);
        process_destroy(p);
    }
}

static void sched_wake_sleepers(void) {
    uint64_t now = get_ticks();
    list_node_t *curr = sleep_queue.node.next;
    
    while (curr != &sleep_queue.node) {
        list_node_t *next = curr->next; // Save next because might move curr
        process_t *p = container_of(curr, process_t, node);
        
        if (p->wake_tick <= now) {
            // Wake up
            list_remove(curr);
            p->process_state = PROCESS_READY;
            list_add_tail(curr, &run_queue.node);
        }
        curr = next;
    }
}

void schedule() {
    // Reap deferred zombies even when the idle loop is not reached.
    sched_reap();

    sched_wake_sleepers();

    process_t *prev = current_process;
    if (!prev) prev = &idle_proc;  // first call: save boot stack into idle_proc

    if (current_process != NULL) {
        if (current_process->process_state == PROCESS_RUNNING) {
            current_process->process_state = PROCESS_READY;
            list_add_tail(&current_process->node, &run_queue.node);
        }
    }

    if (list_empty(&run_queue)) {
        current_process = NULL;
        context_switch(prev, &idle_proc);
        return;
    }

    list_node_t *next_node = list_pop_front(&run_queue);
    current_process = container_of(next_node, process_t, node);
    current_process->process_state = PROCESS_RUNNING;

    if (current_process->as && (!prev || prev->as != current_process->as)) {
        vmm_activate(current_process->as);
    }
    context_switch(prev, current_process);
}

size_t sched_ready_queue_snapshot(process_t **out, size_t max_out) {
    size_t total = 0;
    list_node_t *node = run_queue.node.next;

    while (node != &run_queue.node) {
        if (out && total < max_out) {
            out[total] = container_of(node, process_t, node);
        }
        total++;
        node = node->next;
    }

    return total;
}

void set_resched_flag(void) {
    do_resched = 1;
}