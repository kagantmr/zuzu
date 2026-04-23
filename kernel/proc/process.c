#include "process.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/mmu/mmu.h"
#include "kernel/irq/sys_irq.h"
#include "kernel/sched/sched.h"
#include "kernel/syspage.h"
#include "zuzu/ipcx.h"
#include <mem.h>
#include <string.h>
#include "kstack.h"
#include "core/panic.h"
#include "zuzu/syscall_nums.h"

uint32_t next_pid = 1;
process_t *process_table[MAX_PROCESSES];
extern endpoint_t *nametable_endpoint;

void process_track_reply_cap(process_t *caller, process_t *holder,
                             uint32_t holder_slot, reply_cap_t *rc)
{
    rc->caller_pid = caller ? caller->pid : 0;
    rc->holder_pid = holder ? holder->pid : 0;
    rc->holder_slot = holder_slot;
    rc->caller_link.prev = NULL;
    rc->caller_link.next = NULL;
    list_add_tail(&rc->caller_link, &caller->outstanding_replies.node);
}

process_t *process_create(const char* name) {
    process_t *process = kmalloc(sizeof(process_t));
    if (!process)
        return NULL;
    memset(process, 0, sizeof(process_t));

    if (!handle_vec_init(&process->handle_table))
        goto fail_process;

    process->as = as_create(ADDRSPACE_USER);
    if (!process->as)
        goto fail_handles;

    uint32_t *kstack = (uint32_t *)kstack_alloc();
    if (!kstack)
        goto fail_kstack;
    process->kernel_stack_top = (uintptr_t)kstack;

    // map syspage into user space
    if (!kmap_user_page(process->as, syspage_pa(), 0x1000, VM_PROT_READ))
        goto fail_as;

    vm_region_t sys_region = {
        .vaddr_start = 0x1000,
        .size = PAGE_SIZE,
        .prot = VM_PROT_READ | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_SHARED,
        .flags = VM_FLAG_NONE,
    };
    if (!vmm_add_region(process->as, &sys_region))
        goto fail_as;

    // map IPCX transfer buffer into as
    process->ipc_buf_pa = pmm_alloc_page();
    if (!process->ipc_buf_pa)
        goto fail_as;

    if (!kmap_user_page(process->as, process->ipc_buf_pa, IPCX_BUF_VA,
                        VM_PROT_READ | VM_PROT_WRITE))
        goto fail_as;

    vm_region_t ipc_region = {
        .vaddr_start = IPCX_BUF_VA,
        .size = PAGE_SIZE,
        .prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_ANON,
        .flags = VM_FLAG_NONE,
    };
    if (!vmm_add_region(process->as, &ipc_region))
        goto fail_as;

    // slot 0 is reserved for nametable endpoint when available
    handle_entry_t *slot0 = handle_vec_get(&process->handle_table, 0);
    if (!slot0)
        goto fail_as;

    if (nametable_endpoint && nametable_endpoint->alive) {
        slot0->type = HANDLE_ENDPOINT;
        slot0->grantable = true;
        slot0->mapped_va = 0;
        slot0->ep = nametable_endpoint;
        nametable_endpoint->ref_count++;
    } else {
        slot0->type = HANDLE_FREE;
        slot0->grantable = false;
        slot0->mapped_va = 0;
        slot0->ep = NULL;
    }

    process->process_state = PROCESS_STOPPED;
    process->device_va_next = 0x60000000;
    process->mmap_va_next = 0x20000000;
    process->parent_pid = 0;
    list_init(&process->outstanding_replies);
    list_init(&process->children);
    process->priority = 1;
    process->time_slice = 5;
    process->ticks_remaining = process->time_slice;
    process->wake_tick = 0;
    process->wake_reason = WAKE_NONE;
    process->ipc_state = IPC_NONE;
    process->blocked_endpoint = NULL;
    process->flags = 0;

    if (name) {
        const char *short_name = name;
        for (const char *p = name; *p; p++) {
            if (*p == '/')
                short_name = p + 1;
        }
        strncpy(process->name, short_name, sizeof(process->name) - 1);
    }

    uint32_t start = next_pid % MAX_PROCESSES;
    uint32_t slot = start;
    do {
        if (process_table[slot] == NULL)
            break;
        next_pid++;
        slot = next_pid % MAX_PROCESSES;
    } while (slot != start);

    if (process_table[slot] != NULL)
        goto fail_as;

    process->pid = next_pid++;
    process_table[slot] = process;
    return process;

fail_kstack:
    kstack_free(process->kernel_stack_top);
fail_as:
    if (process->as)
        arch_mmu_free_user_pages(process->as);
    as_destroy(process->as);
fail_handles:
    if (nametable_endpoint) {
        handle_entry_t *maybe_slot0 = handle_vec_get(&process->handle_table, 0);
        if (maybe_slot0 && maybe_slot0->type == HANDLE_ENDPOINT && maybe_slot0->ep == nametable_endpoint) {
            if (nametable_endpoint->ref_count > 0)
                nametable_endpoint->ref_count--;
        }
    }
    handle_vec_destroy(&process->handle_table);
fail_process:
    kfree(process);
    return NULL;
}

void process_untrack_reply_cap(reply_cap_t *rc)
{
    if (!rc)
        return;

    if (rc->caller_link.prev && rc->caller_link.next)
        list_remove(&rc->caller_link);

    rc->caller_link.prev = NULL;
    rc->caller_link.next = NULL;
    rc->caller_pid = 0;
    rc->holder_pid = 0;
    rc->holder_slot = 0;
}

static void process_revoke_outstanding_reply_caps(process_t *caller)
{
    while (!list_empty(&caller->outstanding_replies)) {
        list_node_t *node = list_pop_front(&caller->outstanding_replies);
        reply_cap_t *rc = container_of(node, reply_cap_t, caller_link);

        process_t *holder = process_find_by_pid(rc->holder_pid);
        if (holder) {
            handle_entry_t *entry =
                handle_vec_get(&holder->handle_table, rc->holder_slot);

            if (entry && entry->type == HANDLE_REPLY && entry->reply == rc) {
                entry->reply = NULL;
                entry->grantable = false;
                entry->type = HANDLE_FREE;
            }
        }

        rc->caller_link.prev = NULL;
        rc->caller_link.next = NULL;
        rc->caller_pid = 0;
        rc->holder_pid = 0;
        rc->holder_slot = 0;
        kfree_reply_cap(rc);
    }
}

#define LOG_FMT(fmt) "(proc) " fmt
#include "core/log.h"

process_t *process_find_by_pid(uint32_t pid)
{
    uint32_t slot = pid % MAX_PROCESSES;
    process_t *p = process_table[slot];
    if (p && p->pid == pid)
        return p;
    return NULL;
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

    p->process_state = PROCESS_ZOMBIE;
    p->exit_status = exit_status;

    // Clean up handle table
    for (uint32_t i = 0; i < p->handle_table.cap; i++) {
        handle_entry_t *entry = handle_vec_get(&p->handle_table, i);
        if (!entry)
            break;

        if (entry->type == HANDLE_ENDPOINT) {
            endpoint_t *ep = entry->ep;
            if (ep && ep->owner_pid == p->pid && ep->alive) {
                ep->alive = false;
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
            }
            if (ep) {
                if (ep->ref_count > 0)
                    ep->ref_count--;
                if (ep->ref_count == 0)
                    kfree_endpoint(ep);
            }
            entry->ep = NULL;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
        } else if (entry->type == HANDLE_DEVICE) {
            if (entry->dev) {
                if (entry->dev->ref_count > 0)
                    entry->dev->ref_count--;
                if (entry->dev->ref_count == 0)
                    kfree_device_cap(entry->dev);
            }
            entry->dev = NULL;
            entry->mapped_va = 0;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
        } else if (entry->type == HANDLE_SHMEM) {
            shmem_t *shm = entry->shm;
            const uintptr_t va = entry->mapped_va;
            if (shm && va != 0)
                vmm_remove_region(p->as, va, shm->page_count * PAGE_SIZE);
            if (shm)
                shm->ref_count--;
            if (shm && shm->ref_count == 0) {
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
            process_t *caller = process_find_by_pid(rc ? rc->caller_pid : 0);

            if (caller && caller->ipc_state == IPC_WAITING) {
                caller->ipc_state = IPC_NONE;
                caller->blocked_endpoint = NULL;
                caller->trap_frame->r[0] = ERR_DEAD;
                caller->process_state = PROCESS_READY;
                sched_add(caller);
            }

            if (rc) {
                process_untrack_reply_cap(rc);
                kfree_reply_cap(rc);
            }

            entry->reply = NULL;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
        } else if (entry->type == HANDLE_NOTIFICATION) {
            notification_t *ntfn = entry->ntfn;
            if (ntfn && ntfn->owner_pid == p->pid && ntfn->alive) {
                ntfn->alive = false;
                while (!list_empty(&ntfn->wait_queue)) {
                    list_node_t *n = list_pop_front(&ntfn->wait_queue);
                    process_t *proc = container_of(n, process_t, node);
                    proc->trap_frame->r[0] = ERR_DEAD;
                    proc->process_state = PROCESS_READY;
                    sched_add(proc);
                }
            }
            if (ntfn) {
                if (ntfn->ref_count > 0)
                    ntfn->ref_count--;
                if (ntfn->ref_count == 0)
                    kfree(ntfn);
            }
            entry->ntfn = NULL;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
        } else if (entry->type == HANDLE_TASK) {
            // No special cleanup needed for task handles since they don't have kernel objects associated with them
            entry->task = NULL;
            entry->mapped_va = 0;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
        }
    }

    process_revoke_outstanding_reply_caps(p);

    process_t *init_proc = process_find_by_pid(1);
    list_node_t *child_node = p->children.node.next;
    while (child_node != &p->children.node) {
        list_node_t *next = child_node->next;
        process_t *child = container_of(child_node, process_t, sibling_node);
        process_set_parent(child, init_proc);
        child_node = next;
    }

    // Remove process from whichever queue currently owns p->node, if any.
    if (p->node.prev && p->node.next)
        list_remove(&p->node);
    p->ipc_state = IPC_NONE;
    p->blocked_endpoint = NULL;

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

    // extern pmm_state_t pmm_state;
    irq_release_all(p);
    // KDEBUG("destroy PID %d, pmm before=%d", pid, pmm_state.free_pages);
    //(void)pmm_state;
    if (p->sibling_node.prev && p->sibling_node.next)
        list_remove(&p->sibling_node);
    if (p->as)
    {
        arch_mmu_free_user_pages(p->as);
        as_destroy(p->as);
    }
    handle_vec_destroy(&p->handle_table);
    if (p->kernel_stack_top)
        kstack_free(p->kernel_stack_top);
    process_table[p->pid % MAX_PROCESSES] = NULL;
    kfree(p);
    // KDEBUG("destroy PID %d, pmm after=%d", pid, pmm_state.free_pages);
}
