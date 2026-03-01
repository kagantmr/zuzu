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

void process_kill(process_t *p, int exit_status) {
    p->process_state = PROCESS_ZOMBIE;
    p->exit_status = exit_status;

    process_t *parent = process_find_by_pid(p->parent_pid);
    if (parent && parent->process_state == PROCESS_BLOCKED 
              && parent->waiting_for == p->pid) {
        parent->process_state = PROCESS_READY;
        parent->waiting_for = 0;
        sched_add(parent);
    } else {
        sched_defer_destroy(p);
    }
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