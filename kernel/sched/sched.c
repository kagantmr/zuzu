#include "sched.h"
#include "kernel/proc/process.h"
#include "lib/list.h"
#include "core/log.h"
#include "kernel/vmm/vmm.h"

static list_head_t run_queue = LIST_HEAD_INIT(run_queue); 
static list_head_t destroy_queue = LIST_HEAD_INIT(destroy_queue);
process_t *current_process;

void sched_init() {
    list_init(&run_queue);
    current_process = NULL;
}
void sched_add(process_t *p) {
    list_add_tail(&p->node, &run_queue.node);
}

void sched_defer_destroy(process_t *p) {
    list_add_tail(&p->node, &destroy_queue.node);
}

void sched_reap_zombies(void) {
    while (!list_is_empty(&destroy_queue)) {
        list_node_t *node = list_remove_head(&destroy_queue);
        process_t *p = container_of(node, process_t, node);
        process_destroy(p);
    }
}

void schedule() {
    if (current_process != NULL) {
        // Save current process state
        current_process->process_state = PROCESS_READY;
        list_add_tail(&current_process->node, &run_queue.node);
    }

    if (list_is_empty(&run_queue)) {
        // No process to schedule
        current_process = NULL;
        return;
    }

    process_t *prev = current_process;  // save previous process

    // Pick the next process from the run queue
    list_node_t *next_node = list_remove_head(&run_queue);
    current_process = container_of(next_node, process_t, node);
    current_process->process_state = PROCESS_RUNNING;

    // Context switch to the new process (not implemented here)
    // KDEBUG("schedule: prev=%P next=%P", prev, current_process);
    if (current_process->as && (!prev || prev->as != current_process->as)) {
        vmm_activate(current_process->as);
    }
    context_switch(prev, current_process);  // need to save prev first!
}