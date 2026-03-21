#include "process.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/mmu/mmu.h"
#include "kernel/irq/sys_irq.h"
#include "kernel/sched/sched.h"
#include <mem.h>
#include "kstack.h"
#include "core/panic.h"
#include "zuzu/syscall_nums.h"

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

void process_kill(process_t *p, const int exit_status) {
    if (p->flags & (PROC_FLAG_INIT | PROC_FLAG_DEVMGR)) {
        panic("Attempted to kill critical process");
    }
    // Clean up handle table — clear non-owned handles, free owned endpoints
    for (uint32_t i = 0; i < p->handle_table.cap; i++) {
        handle_entry_t *entry = handle_vec_get(&p->handle_table, i);
        if (!entry)
            break;

        if (entry->type == HANDLE_ENDPOINT) {
            endpoint_t *ep = entry->ep;
            if (ep && ep->owner_pid == p->pid) {
                // Wake blocked waiters with ERR_DEAD
                while (!list_empty(&ep->sender_queue)) {
                    list_node_t *n = list_pop_front(&ep->sender_queue);
                    process_t *proc = container_of(n, process_t, node);
                    proc->ipc_state = IPC_NONE;
                    proc->blocked_endpoint = NULL;
                    proc->trap_frame->r[0] = ERR_DEAD;
                    proc->process_state = PROCESS_READY;
                    sched_add(proc);
                }
                while (!list_empty(&ep->receiver_queue)) {
                    list_node_t *n = list_pop_front(&ep->receiver_queue);
                    process_t *proc = container_of(n, process_t, node);
                    proc->ipc_state = IPC_NONE;
                    proc->blocked_endpoint = NULL;
                    proc->trap_frame->r[0] = ERR_DEAD;
                    proc->process_state = PROCESS_READY;
                    sched_add(proc);
                }
                kfree(ep);
            }
            entry->ep = NULL;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
        } else if (entry->type == HANDLE_SHMEM) {
            shmem_t *shm = entry->shm;
            const uintptr_t va = entry->mapped_va;
            vmm_remove_region(p->as, va, shm->page_count * PAGE_SIZE);
            for (size_t j = 0; j < shm->page_count; j++)
                vmm_unmap_range(p->as, va + j * PAGE_SIZE, PAGE_SIZE);
            shm->ref_count--;
            if (shm->ref_count == 0) {
                for (size_t j = 0; j < shm->page_count; j++)
                    pmm_free_page(shm->page_addrs[j]);
                kfree(shm->page_addrs);
                kfree(shm);
            }
            entry->shm = NULL;
            entry->mapped_va = 0;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
        }
    }

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
    handle_vec_destroy(&p->handle_table);
    process_table[pid] = NULL;
    kstack_free(p->kernel_stack_top);
    kfree(p);
    // KDEBUG("destroy PID %d, pmm after=%d", pid, pmm_state.free_pages);
}