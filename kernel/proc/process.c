#include "process.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"

static uint32_t next_pid = 1;

process_t* process_create(void (*entry)(void), const uint32_t magic) {
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
    process->as = addrspace_create(ADDRSPACE_USER);
    process->priority = 1; // Default priority
    process->time_slice = 5; // Default time slice
    process->ticks_remaining = process->time_slice;
    process->node.next = NULL;
    process->node.prev = NULL;

    uintptr_t program_page_pa = pmm_alloc_page();
     *(volatile uint32_t *)(PA_TO_VA(program_page_pa)) = magic;

    kmap_user_page(process->as, program_page_pa, 0x10000, VM_PROT_READ | VM_PROT_WRITE);

    // user VA for code should start at first page, stack could be ...idk? N=1 means we get a 2gb/2gb split
    // allocate those
    // copy code, memcpy() probably
    // copy stack, memcpy() prob.
    // set entry point    

    return process;
}
