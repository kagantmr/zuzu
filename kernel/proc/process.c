#include "process.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/mmu/mmu.h"
#include "kernel/irq/sys_irq.h"
#include "kernel/sched/sched.h"
#include "lib/mem.h"
#include "kstack.h"

uint32_t next_pid = 1;
process_t *process_table[MAX_PROCESSES];

#define LOG_FMT(fmt) "(proc) " fmt
#include "core/log.h"

process_t *process_find_by_pid(uint32_t pid)
{
    if (pid >= MAX_PROCESSES)
        return NULL;
    return process_table[pid];
}

process_t *process_create(void (*entry)(void))
{
    (void)entry;
    process_t *process = kmalloc(sizeof(process_t));
    if (!process)
        return NULL;
    memset(process, 0, sizeof(process_t));

    // try creating as
    process->as = addrspace_create(ADDRSPACE_USER);
    if (!process->as)
        goto fail_process;

    // try getting a kstack
    uintptr_t stack_top = kstack_alloc();
    if (!stack_top)
        goto fail_as;
    process->kernel_stack_top = stack_top;

    // write code & map it
    uintptr_t program_page_pa = pmm_alloc_page();
    if (!program_page_pa)
        goto fail_kstack;

    if (!kmap_user_page(process->as, program_page_pa, 0x10000,
                        VM_PROT_READ | VM_PROT_WRITE))
    {
        pmm_free_page(program_page_pa); // orphan: never mapped
        goto fail_kstack;
    }
    // get user stack
    uintptr_t user_stack_pa = pmm_alloc_pages(4);
    if (!user_stack_pa)
        goto fail_kstack;

    for (int i = 0; i < 4; i++)
    {
        if (!kmap_user_page(process->as, user_stack_pa + i * 0x1000,
                            0x7FFFC000 + i * 0x1000,
                            VM_PROT_READ | VM_PROT_WRITE))
        {
            // free unmapped remainder (orphans)
            for (int j = i; j < 4; j++)
                pmm_free_page(user_stack_pa + j * 0x1000);
            goto fail_kstack;
        }
    }

    // write exception frame to the stack
    stack_top -= 16 * sizeof(uint32_t); // 16 words
    uint32_t *exc_frame = (uint32_t *)stack_top;
    *(exc_frame++) = 0;
    for (int i = 0; i < 12; i++)
    {
        *(exc_frame++) = 0; // r1-12 = 0  (indices 0-12)
    }
    *(exc_frame++) = USR_SP;  // lr = SP_usr              (index 13)
    *(exc_frame++) = 0x10000; // PC = entry point    (index 14)
    *(exc_frame++) = 0x10;    // CPSR = 0x10         (index 15)

    // write cpu_context to stack
    stack_top -= sizeof(cpu_context_t);
    cpu_context_t *context = (cpu_context_t *)stack_top;
    context->r4 = 0;
    context->r5 = 0;
    context->r6 = 0;
    context->r7 = 0;
    context->r8 = 0;
    context->r9 = 0;
    context->r10 = 0;
    context->r11 = 0;
    context->lr = (uint32_t)process_entry_trampoline;

    process->kernel_sp = (uint32_t *)stack_top;
    process->process_state = PROCESS_READY;
    process->pid = next_pid++;
    process_table[process->pid] = process;
    process->device_va_next = 0x60000000;
    process->parent_pid = 0;

    process->priority = 1;
    process->time_slice = 5;
    process->ticks_remaining = process->time_slice;
    process->node.next = NULL;
    process->node.prev = NULL;

    // KDEBUG("Created process with magic %X and PID %d", magic, process->pid);
    return process;

fail_kstack:
    kstack_free(process->kernel_stack_top);
fail_as:
    addrspace_destroy(process->as);
fail_process:
    kfree(process);
    return NULL;
}

void process_destroy(process_t *p)
{

    uint32_t pid = p->pid; // apparently causes an UAF
    // extern pmm_state_t pmm_state;
    irq_release_all(p);
    // KDEBUG("destroy PID %d, pmm before=%d", pid, pmm_state.free_pages);
    //(void)pmm_state;
    if (p->as)
    {
        arch_mmu_free_user_pages(p->as->ttbr0_pa);
        arch_mmu_free_tables(p->as->ttbr0_pa, p->as->type);
        if (p->as->regions)
            kfree(p->as->regions);
        kfree(p->as);
    }
    process_table[pid] = NULL;
    kstack_free(p->kernel_stack_top);
    kfree(p);
    // KDEBUG("destroy PID %d, pmm after=%d", pid, pmm_state.free_pages);
}