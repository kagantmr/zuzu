#include "process.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"

static uint32_t next_pid = 1;

process_t* process_create(void (*entry)(void), const uint32_t magic) {
    (void) entry;
    process_t* process = kmalloc(sizeof(process_t));
    process->as = addrspace_create(ADDRSPACE_USER);
    uint32_t* kernel_stack = kmalloc(4096); // Allocate 4KB for kernel stack

    uintptr_t stack_top = (uintptr_t)kernel_stack + 4096;

    // write exception frame to the stack
    stack_top -= 16 * sizeof(uint32_t);    // 16 words
    uint32_t *exc_frame = (uint32_t *)stack_top;
    *(exc_frame++) = 0;
    for (int i = 0; i < 12; i++) {
        *(exc_frame++) = 0; // r1-12 = 0  (indices 0-12)
    }
    *(exc_frame++) = 0x7FFFF000; // lr = SP_usr              (index 13)
    *(exc_frame++) = 0x10000; // PC = entry point    (index 14)
    *(exc_frame++) = 0x10; // CPSR = 0x10         (index 15)

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
    context->lr = (uint32_t)process_entry_trampoline;

    process->kernel_sp = (uint32_t*)stack_top;
    process->process_state = PROCESS_READY;
    process->pid = next_pid++;
    process->parent_pid = 0; // No parent for now
    
    process->priority = 1; // Default priority
    process->time_slice = 5; // Default time slice
    process->ticks_remaining = process->time_slice;
    process->node.next = NULL;
    process->node.prev = NULL;

    uintptr_t program_page_pa = pmm_alloc_page();
     *(volatile uint32_t *)(PA_TO_VA(program_page_pa)) = magic;

    kmap_user_page(process->as, program_page_pa, 0x10000, VM_PROT_READ | VM_PROT_WRITE);
    
    uintptr_t user_stack_pa = pmm_alloc_pages(4);
    for (int i = 0; i < 4; i++) {
        kmap_user_page(process->as, user_stack_pa + i * 0x1000,
                    0x7FFFC000 + i * 0x1000, VM_PROT_READ | VM_PROT_WRITE);
    }

    // EXPERIMENTAL PROCESS TEST!!!!!!
    // TODO: Clean up after ELF loading is activated
    uint32_t *code = (uint32_t *)(PA_TO_VA(program_page_pa));
    if (magic == 0xDEADBEEF) {
        code[0] = 0xE3A00001;   // MOV R0, 1
        code[1] = 0xEF000000;  // svc #255
        code[2] = 0xEAFFFFFE;  // b .
        code[3] = 0xEAFFFFFE;  // b .
    } else {
        // Process B/C: Force Data Abort at 0xC0000000
        
        // 1. MOV R0, #1
        code[0] = 0xE3A00001; 
        
        // 2. MOVT R0, #0xC000 (R0 becomes 0xC0000000)
        code[1] = 0xEE010F10; // mcr p15, 0, r0, c1, c0, 0
        code[2] = 0xEAFFFFFE; 
        // 4. b .              (In case it somehow survives)
        code[3] = 0xEF000000;
    }

    // user VA for code should start at first page, stack could be ...idk? N=1 means we get a 2gb/2gb split
    // allocate those
    // copy code, memcpy() probably
    // copy stack, memcpy() prob.
    // set entry point    

    return process;
}
