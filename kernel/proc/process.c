#include "process.h"
#include "kernel/mm/alloc.h"

static uint32_t next_pid = 1;

process_t* process_create(void (*entry)(void)) {
    process_t* process = kmalloc(sizeof(process_t));
    uint32_t* kernel_stack = kmalloc(4096); // Allocate 4KB for kernel stack

    uintptr_t stack_top = (uintptr_t)kernel_stack + 4096;

    // write cpu_context to stack
    stack_top -= sizeof(cpu_context_t);
    cpu_context_t* context = (cpu_context_t*)stack_top;
    context->r4 = 0;
    context->r5 = 0;
    context->r6 = 0;
    context->r7 = 0;
    context->r8 = 0;
    context->r9 = 0;
    context->r10 = 0;
    context->r11 = 0;
    context->lr = (uint32_t)entry;
    process->kernel_sp = (uint32_t*)stack_top;
    process->process_state = PROCESS_READY;
    process->pid = next_pid++;
    process->parent_pid = 0; // No parent for now
    process->ttbr0 = 0; // Placeholder, should be set to actual page
    process->priority = 1; // Default priority
    process->time_slice = 5; // Default time slice
    process->ticks_remaining = process->time_slice;
    process->node.next = NULL;
    process->node.prev = NULL;

    return process;
}
