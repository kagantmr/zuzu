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

static process_t *process_resolve_live_ptr(process_t *candidate)
{
    if (!candidate)
        return NULL;

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *live = process_table[i];
        if (live == candidate)
            return live;
    }
    return NULL;
}

#define LOG_FMT(fmt) "(proc) " fmt
#include "core/log.h"

process_t *process_find_by_pid(uint32_t pid)
{
    if (pid >= MAX_PROCESSES)
        return NULL;
    return process_table[pid];
}

void process_set_parent(process_t *child, process_t *parent)
{
    if (!child)
        return;

    if (child->sibling_node.prev && child->sibling_node.next)
        list_remove(&child->sibling_node);

    child->parent_pid = parent ? parent->pid : 0;

    if (parent)
        list_add_tail(&child->sibling_node, &parent->children.node);
}

process_t *process_find_child_by_pid(process_t *parent, uint32_t pid)
{
    if (!parent)
        return NULL;

    list_node_t *node = parent->children.node.next;
    while (node != &parent->children.node) {
        process_t *child = container_of(node, process_t, sibling_node);
        if (child->pid == pid)
            return child;
        node = node->next;
    }

    return NULL;
}

process_t *process_find_zombie_child(process_t *parent)
{
    if (!parent)
        return NULL;

    list_node_t *node = parent->children.node.next;
    while (node != &parent->children.node) {
        process_t *child = container_of(node, process_t, sibling_node);
        if (child->process_state == PROCESS_ZOMBIE)
            return child;
        node = node->next;
    }

    return NULL;
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
                // A process may hold multiple handle slots that alias the same
                // owned endpoint. Collapse aliases so teardown frees each
                // endpoint object at most once.
                for (uint32_t j = i + 1; j < p->handle_table.cap; j++) {
                    handle_entry_t *alias = handle_vec_get(&p->handle_table, j);
                    if (!alias)
                        break;
                    if (alias->type == HANDLE_ENDPOINT && alias->ep == ep) {
                        alias->ep = NULL;
                        alias->grantable = false;
                        alias->type = HANDLE_FREE;
                    }
                }

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
        } else if (entry->type == HANDLE_DEVICE) {
            if (entry->dev)
                kfree(entry->dev);
            entry->dev = NULL;
            entry->mapped_va = 0;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
        } else if (entry->type == HANDLE_SHMEM) {
            shmem_t *shm = entry->shm;
            const uintptr_t va = entry->mapped_va;
            vmm_remove_region(p->as, va, shm->page_count * PAGE_SIZE);
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
        } else if (entry->type == HANDLE_REPLY) {
            reply_cap_t *rc = entry->reply;
            process_t *caller = process_resolve_live_ptr(rc ? rc->caller : NULL);

            if (caller && caller->ipc_state == IPC_WAITING) {
                caller->ipc_state = IPC_NONE;
                caller->blocked_endpoint = NULL;
                caller->trap_frame->r[0] = ERR_DEAD;
                caller->process_state = PROCESS_READY;
                sched_add(caller);
            }

            if (rc)
                kfree(rc);

            entry->reply = NULL;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
        }
    }

    // Revoke outstanding reply capabilities held by other processes for this caller.
    for (uint32_t owner_pid = 0; owner_pid < MAX_PROCESSES; owner_pid++) {
        process_t *owner = process_table[owner_pid];
        if (!owner || owner == p)
            continue;

        for (uint32_t i = 0; i < owner->handle_table.cap; i++) {
            handle_entry_t *entry = handle_vec_get(&owner->handle_table, i);
            if (!entry)
                break;
            if (entry->type != HANDLE_REPLY)
                continue;

            reply_cap_t *rc = entry->reply;
            if (!rc || rc->caller != p)
                continue;

            kfree(rc);
            entry->reply = NULL;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
        }
    }

    process_t *init_proc = process_find_by_pid(1);
    list_node_t *child_node = p->children.node.next;
    while (child_node != &p->children.node) {
        list_node_t *next = child_node->next;
        process_t *child = container_of(child_node, process_t, sibling_node);
        process_set_parent(child, init_proc);
        child_node = next;
    }

    p->process_state = PROCESS_ZOMBIE;
    p->exit_status = exit_status;

    process_t *parent = process_find_by_pid(p->parent_pid);
    if (parent && parent->process_state == PROCESS_BLOCKED 
              && (parent->waiting_for == p->pid || parent->waiting_for == UINT32_MAX)) {
        parent->process_state = PROCESS_READY;
        parent->waiting_for = 0;
        sched_add(parent);
    } else {
        sched_defer_destroy(p);
    }
}

void process_destroy(process_t *p)
{

    uint32_t pid = p->pid;
    // extern pmm_state_t pmm_state;
    irq_release_all(p);
    // KDEBUG("destroy PID %d, pmm before=%d", pid, pmm_state.free_pages);
    //(void)pmm_state;
    if (p->sibling_node.prev && p->sibling_node.next)
        list_remove(&p->sibling_node);
    if (p->as)
    {
        arch_mmu_free_user_pages(p->as->ttbr0_pa);
        arch_mmu_free_tables(p->as->ttbr0_pa, p->as->type);
        vm_region_vec_destroy(&p->as->regions);
        kfree(p->as);
    }
    handle_vec_destroy(&p->handle_table);
    process_table[pid] = NULL;
    kstack_free(p->kernel_stack_top);
    kfree(p);
    // KDEBUG("destroy PID %d, pmm after=%d", pid, pmm_state.free_pages);
}