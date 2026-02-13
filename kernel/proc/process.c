#include "process.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/mmu/mmu.h"

static uint32_t next_pid = 1;

process_t* process_create(void (*entry)(void), const uint32_t magic) {
    (void) entry;
    process_t* process = kmalloc(sizeof(process_t));
    process->as = addrspace_create(ADDRSPACE_USER);
    uint32_t* kernel_stack = kmalloc(4096); // Allocate 4KB for kernel stack
    process->kernel_stack_base = (uintptr_t)kernel_stack;   // track for later free
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
        code[0] = 0xE3A00000;  // MOV R0, #0
        code[1] = 0xE34C0000;  // MOVT R0, #0xC000
        code[2] = 0xE3A0100A;  // MOV R1, #10
        code[3] = 0xEF0000F0;  // SVC #0xF0 (sys_log)
        code[4] = 0xE3A00000;  // MOV R0, #0
        code[5] = 0xEF000000;  // SVC #0x00 (sys_task_quit)
        code[6] = 0xEAFFFFFE;  // B . (safety)
    } else if (magic == 0xCAFEBABE){
        // Pass kernel address 0xC0000000 to sys_log
        code[0] = 0xE3A00000;  // MOV R0, #0
        code[1] = 0xE34C0000;  // MOVT R0, #0xC000  (R0 = 0xC0000000)
        code[2] = 0xE3A0100A;  // MOV R1, #10
        code[3] = 0xEF0000F0;  // SVC #0xF0 (sys_log — should be rejected)
        code[4] = 0xE3A00000;  // MOV R0, #0
        code[5] = 0xEF000000;  // SVC #0x00 (sys_task_quit)
        code[6] = 0xEAFFFFFE;  // B . (safety)
    } else {
        // Call a syscall that doesn't exist
        code[0] = 0xEF0000FE;  // SVC #0xFE (not in dispatch table)
        code[1] = 0xE3A00000;  // MOV R0, #0
        code[2] = 0xEF000000;  // SVC #0x00 (sys_task_quit)
        code[3] = 0xEAFFFFFE;  // B . (safety)
    }

    // user VA for code should start at first page, stack could be ...idk? N=1 means we get a 2gb/2gb split
    // allocate those
    // copy code, memcpy() probably
    // copy stack, memcpy() prob.
    // set entry point    

    return process;
}

void process_destroy(process_t *p) {
    if (p->as) {
        arch_mmu_free_user_pages(p->as->ttbr0_pa);  // arch-specific
        arch_mmu_free_tables(p->as->ttbr0_pa, p->as->type);       // already exists
        if (p->as->regions) kfree(p->as->regions);   // generic
        kfree(p->as);                                 // generic
    }
    kfree((void *)p->kernel_stack_base);              // generic
    kfree(p);                                         // generic
}

