#include "sched.h"
#include "lib/list.h"
#include "core/log.h"
#include "kernel/vmm/vmm.h"

static list_head_t run_queue = LIST_HEAD_INIT(run_queue); 
process_t *current_process;

void sched_init() {
    list_init(&run_queue);
    current_process = NULL;
}
void sched_add(process_t *p) {
    list_add_tail(&p->node, &run_queue.node);
}
void schedule() {
    KINFO("schedule: current=%p", current_process);
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

    if (prev && prev->pid > 0) {
        // The saved registers are on prev's kernel stack above kernel_sp
        // After context_switch saves {r4-r11, lr}, the exception frame is above that
        uint32_t *exc = (uint32_t *)((uintptr_t)prev->kernel_sp + sizeof(cpu_context_t));
        KINFO("PID %d preempted, r0=0x%08x", prev->pid, exc[0]);
    }

    // Context switch to the new process (not implemented here)
    KINFO("schedule: prev=%p next=%p", prev, current_process);
    if (current_process->as && (!prev || prev->as != current_process->as)) {
        vmm_activate(current_process->as);
    }
    context_switch(prev, current_process);  // need to save prev first!
}