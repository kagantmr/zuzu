#include "process.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include <arch/mmu.h>
#include "kernel/irq/sys_irq.h"
#include "kernel/sched/sched.h"
#include "kernel/syspage.h"
#include "zuzu/ipcx.h"
#include <zuzu/user_layout.h>
#include <mem.h>
#include <string.h>
#include <stdint.h>
#include <elf.h>
#include "kstack.h"
#include "core/panic.h"
#include "zuzu/syscall_nums.h"
#include <arch/cache.h>

uint32_t next_pid = 1;
process_t *process_table[MAX_PROCESSES];
extern endpoint_t *nametable_endpoint;

#define LOG_FMT(fmt) "(proc) " fmt
#include "core/log.h"

static bool elf_segment_ranges_overlap(const Elf32_Phdr *a, const Elf32_Phdr *b)
{
    uint32_t a_start = a->p_vaddr;
    uint32_t b_start = b->p_vaddr;
    uint32_t a_end = a->p_vaddr + a->p_memsz;
    uint32_t b_end = b->p_vaddr + b->p_memsz;

    if (a_end < a_start || b_end < b_start)
        return true;

    return (a_start < b_end) && (b_start < a_end);
}

process_t *process_load(const void *elf_data, size_t elf_size,
                                   const char *name, const char *argbuf,
                                   size_t argbuf_len, uint32_t argc)
{
    uint32_t elf_entry = elf_validate(elf_data, elf_size);
    if (!elf_entry)
        return NULL;

    process_t *p = process_create(name);
    if (!p)
        return NULL;
    thread_t *t = p->thread;
    if (!t)
        goto fail_process;
    vaddr_t stack_top = t->kernel_stack_top;

    for (int i = 0; i < elf_phdr_count(elf_data); i++)
    {
        Elf32_Phdr *ph_i = elf_phdr_get(elf_data, i);
        if (ph_i->p_type != PT_LOAD)
            continue;

        for (int j = i + 1; j < elf_phdr_count(elf_data); j++)
        {
            Elf32_Phdr *ph_j = elf_phdr_get(elf_data, j);
            if (ph_j->p_type != PT_LOAD)
                continue;

            if (elf_segment_ranges_overlap(ph_i, ph_j))
            {
                KERROR("ELF load segments overlap: [%08X, %08X) and [%08X, %08X)",
                       ph_i->p_vaddr, ph_i->p_vaddr + ph_i->p_memsz,
                       ph_j->p_vaddr, ph_j->p_vaddr + ph_j->p_memsz);
                goto fail_kstack;
            }
        }
    }

    for (int i = 0; i < elf_phdr_count(elf_data); i++)
    {
        Elf32_Phdr *ph = elf_phdr_get(elf_data, i);
        if (ph->p_type == PT_LOAD)
        {
            if (ph->p_offset + ph->p_filesz > elf_size)
            {
                goto fail_kstack;
            }
            size_t pages_needed = (ph->p_memsz + PAGE_SIZE - 1) / PAGE_SIZE;
            uintptr_t *segment_pages = kmalloc(pages_needed * sizeof(uintptr_t));
            if (!segment_pages)
                goto fail_kstack;

            uint32_t prot = 0;
            if (ph->p_flags & PF_R)
                prot |= VM_PROT_READ;
            if (ph->p_flags & PF_W)
                prot |= VM_PROT_WRITE;
            if (ph->p_flags & PF_X)
                prot |= VM_PROT_EXEC;

            for (uint32_t page = 0; page < pages_needed; page++)
            {
                uintptr_t page_pa = pmm_alloc_page();
                if (!page_pa)
                {
                    for (uint32_t j = 0; j < page; j++)
                    {
                        uintptr_t orphan_va = ph->p_vaddr + j * PAGE_SIZE;
                        vmm_unmap_range(p->as, orphan_va, PAGE_SIZE);
                        pmm_free_page(segment_pages[j]);
                    }
                    kfree(segment_pages);
                    goto fail_kstack;
                }

                segment_pages[page] = page_pa;

                vaddr_t file_offset = page * PAGE_SIZE;
                size_t bytes_to_copy = 0;
                if (file_offset < ph->p_filesz)
                {
                    bytes_to_copy = ph->p_filesz - file_offset;
                    if (bytes_to_copy > PAGE_SIZE)
                        bytes_to_copy = PAGE_SIZE;

                    memcpy((void *)PA_TO_VA(page_pa),
                           (const uint8_t *)elf_data + ph->p_offset + file_offset,
                           bytes_to_copy);

                    if (bytes_to_copy < PAGE_SIZE)
                    {
                        memset((uint8_t *)PA_TO_VA(page_pa) + bytes_to_copy,
                               0,
                               PAGE_SIZE - bytes_to_copy);
                    }
                }
                else
                {
                    memset((void *)PA_TO_VA(page_pa), 0, PAGE_SIZE);
                }

                vaddr_t va = ph->p_vaddr + page * PAGE_SIZE;
                if (!vmm_map_user_page(p->as, page_pa, va, prot))
                {
                    pmm_free_page(page_pa);
                    for (uint32_t j = 0; j < page; j++)
                    {
                        vaddr_t orphan_va = ph->p_vaddr + j * PAGE_SIZE;
                        vmm_unmap_range(p->as, orphan_va, PAGE_SIZE);
                        pmm_free_page(segment_pages[j]);
                    }
                    kfree(segment_pages);
                    goto fail_kstack;
                }

                arch_cache_flush_code_range((uintptr_t)PA_TO_VA(page_pa), PAGE_SIZE);
            }

            vm_region_t seg_region = {
                .vaddr_start = ph->p_vaddr,
                .size = pages_needed * PAGE_SIZE,
                .prot = prot | VM_PROT_USER,
                .memtype = VM_MEM_NORMAL,
                .owner = VM_OWNER_ANON,
                .flags = VM_FLAG_NONE,
            };
            if (!vmm_add_region(p->as, &seg_region))
            {
                KERROR("Failed to add ELF segment region at VA %08X", ph->p_vaddr);
                kfree(segment_pages);
                goto fail_kstack;
            }

            kfree(segment_pages);
        }
    }

    const vaddr_t user_stack_base = USER_STACK_BASE;
    const vaddr_t user_guard_va = USER_STACK_GUARD_VA;

    paddr_t user_stack_pa = pmm_alloc_pages(USER_STACK_PAGES);
    if (!user_stack_pa)
        goto fail_kstack;

    for (int i = 0; i < (int)USER_STACK_PAGES; i++)
    {
        if (!vmm_map_user_page(p->as, user_stack_pa + i * 0x1000,
                            user_stack_base + i * 0x1000,
                            VM_PROT_READ | VM_PROT_WRITE))
        {
            for (int j = i; j < (int)USER_STACK_PAGES; j++)
                pmm_free_page(user_stack_pa + j * 0x1000);
            goto fail_kstack;
        }
    }

    vm_region_t stack_region = {
        .vaddr_start = user_stack_base,
        .size = USER_STACK_SIZE,
        .prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_ANON,
        .flags = VM_FLAG_NONE,
    };
    if (!vmm_add_region(p->as, &stack_region))
    {
        KERROR("Failed to add user stack region");
        goto fail_kstack;
    }

    vm_region_t guard = {
        .vaddr_start = user_guard_va,
        .size = PAGE_SIZE,
        .prot = 0,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_NONE,
        .flags = VM_FLAG_GUARD,
    };
    if (!vmm_add_region(p->as, &guard))
    {
        KERROR("Failed to add user stack guard region");
        goto fail_kstack;
    }

    vaddr_t sp = USR_SP;
    vaddr_t argv_va = 0;

    const size_t user_stack_size = 4 * PAGE_SIZE;

    if ((argc > 0) != (argbuf && argbuf_len > 0))
    {
        KERROR("Invalid argv payload: argc=%u argbuf_len=%u", argc, (unsigned)argbuf_len);
        goto fail_kstack;
    }

    if (argbuf && argbuf_len > 0 && argc > 0)
    {
        if (((const char *)argbuf)[argbuf_len - 1] != '\0')
        {
            KERROR("Invalid argv payload: missing trailing NUL");
            goto fail_kstack;
        }

        size_t nul_count = 0;
        for (size_t i = 0; i < argbuf_len; i++)
        {
            if (((const char *)argbuf)[i] == '\0')
            {
                nul_count++;
            }
        }
        if (nul_count < argc)
        {
            KERROR("Invalid argv payload: argc exceeds NUL-delimited strings");
            goto fail_kstack;
        }

        size_t argv_slots = (size_t)argc + 1u;
        if (argv_slots <= (size_t)argc)
        {
            KERROR("Invalid argv payload: argc too large");
            goto fail_kstack;
        }

        size_t argv_bytes = argv_slots * sizeof(uint32_t);
        if (argv_bytes / sizeof(uint32_t) != argv_slots)
        {
            KERROR("Invalid argv payload: argv bytes overflow");
            goto fail_kstack;
        }
        vaddr_t check_sp = USR_SP;

        if (check_sp - user_stack_base > user_stack_size ||
            argbuf_len > (size_t)(check_sp - user_stack_base))
        {
            KERROR("argv payload does not fit user stack");
            goto fail_kstack;
        }

        check_sp -= argbuf_len;
        check_sp &= ~((uintptr_t)3u);

        if (argv_bytes > (size_t)(check_sp - user_stack_base))
        {
            KERROR("argv pointer array does not fit user stack");
            goto fail_kstack;
        }

        check_sp -= argv_bytes;
        check_sp &= ~((uintptr_t)7u);

        if (check_sp < user_stack_base)
        {
            KERROR("argv layout underflowed user stack");
            goto fail_kstack;
        }

        sp -= argbuf_len;
        sp &= ~3u;
        vaddr_t strings_va = sp;
        memcpy((void *)PA_TO_VA(user_stack_pa + (strings_va - user_stack_base)),
               argbuf, argbuf_len);

        sp -= (argc + 1) * sizeof(uint32_t);
        sp &= ~7u;
        argv_va = sp;
        uint32_t *argv_kern = (uint32_t *)PA_TO_VA(user_stack_pa + (argv_va - user_stack_base));

        vaddr_t str_va = strings_va;
        for (uint32_t a = 0; a < argc; a++)
        {
            argv_kern[a] = (uint32_t)str_va;
            str_va += strlen((const char *)PA_TO_VA(user_stack_pa + (str_va - user_stack_base))) + 1;
        }
        argv_kern[argc] = 0;
    }

    stack_top -= 17 * sizeof(uint32_t);
    uint32_t *exc_frame = (uint32_t *)stack_top;
    exc_frame[0] = argc;
    exc_frame[1] = (vaddr_t)argv_va;
    for (int i = 2; i < 13; i++)
        exc_frame[i] = 0;
    exc_frame[13] = (vaddr_t)sp;
    exc_frame[14] = USER_ELF_BASE;
    exc_frame[15] = elf_entry;
    exc_frame[16] = 0x10;

    stack_top -= sizeof(cpu_context_t);
    cpu_context_t *context = (cpu_context_t *)stack_top;
    memset(context, 0, sizeof(cpu_context_t));
    context->lr = (uint32_t)process_entry_trampoline;

    stack_top -= 132;
    memset((void *)stack_top, 0, 132);
    t->kernel_sp = (uint32_t *)stack_top;
    t->state = READY;

    KTRACE("process create: pid=%u name=%s tid=%u owner_thread=%p as=%p",
        p->pid,
        p->name,
        p->thread ? p->thread->tid : 0,
        (void *)p->thread,
        (void *)p->as);
    return p;

fail_kstack:
    if (process_table[p->pid % MAX_PROCESSES] == p)
        process_table[p->pid % MAX_PROCESSES] = NULL;

    if (nametable_endpoint) {
        handle_entry_t *slot0 = handle_vec_get(&p->handle_table, 0);
        if (slot0 && slot0->type == HANDLE_ENDPOINT && slot0->ep == nametable_endpoint) {
            if (nametable_endpoint->ref_count > 0)
                nametable_endpoint->ref_count--;
        }
    }

    if (p->as)
        arch_mmu_free_user_pages(p->as);
    as_destroy(p->as);
    handle_vec_destroy(&p->handle_table);
    thread_destroy(t);
fail_process:
    kfree(p);
    return NULL;
}

void process_track_reply_cap(process_t *caller, process_t *holder,
                             handle_t holder_slot, reply_cap_t *rc)
{
    rc->holder_pid = holder ? holder->pid : 0;
    rc->holder_slot = holder_slot;
    rc->caller_link.prev = NULL;
    rc->caller_link.next = NULL;
    list_add_tail(&rc->caller_link, &caller->outstanding_replies.node);
}

process_t *process_create(const char* name) {
    process_t *p = kmalloc(sizeof(process_t));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(process_t));

    list_init(&p->outstanding_replies);
    list_init(&p->threads);
    list_init(&p->children);

    thread_t *t = thread_create(p);
    if (!t)
        goto fail_process;
    p->thread = t;

    if (!handle_vec_init(&p->handle_table))
        goto fail_process;

    p->as = as_create(ADDRSPACE_USER);
    if (!p->as)
        goto fail_handles;

    // map syspage into user space
    if (!vmm_map_user_page(p->as, syspage_pa(), USER_SYSPAGE_VA, VM_PROT_READ))
        goto fail_kstack;

    vm_region_t sys_region = {
        .vaddr_start = USER_SYSPAGE_VA,
        .size = PAGE_SIZE,
        .prot = VM_PROT_READ | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_SHARED,
        .flags = VM_FLAG_NONE,
    };
    if (!vmm_add_region(p->as, &sys_region))
        goto fail_kstack;

    // map IPCX transfer buffer into as
    t->ipc_buf_pa = pmm_alloc_page();
    if (!t->ipc_buf_pa)
        goto fail_kstack;

    if (!vmm_map_user_page(p->as, t->ipc_buf_pa, USER_IPC_BUF_VA,
                        VM_PROT_READ | VM_PROT_WRITE)) // could use a bump pointer instead
        goto fail_kstack;
    
    vm_region_t ipc_region = {
        .vaddr_start = USER_IPC_BUF_VA,
        .size = PAGE_SIZE,
        .prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_ANON,
        .flags = VM_FLAG_NONE,
    };
    if (!vmm_add_region(p->as, &ipc_region))
        goto fail_kstack;

    /* Initialize per-process mmap bump pointer before allocating the
     * TCB mapping so we can place the TCB page at the process's
     * `mmap_va_next` value and then advance it. */
    p->device_va_next = USER_DEVICE_BASE;
    p->mmap_va_next = USER_MMAP_BASE;

    paddr_t tcb_page_pa = pmm_alloc_page();
    if (!tcb_page_pa)
        goto fail_kstack;
    p->tcb_page_pa = tcb_page_pa;
    /* Map the TCB page into the user mmap area at the process's bump
     * pointer so userspace can read its per-thread slot. */
    vaddr_t tcb_user_va = p->mmap_va_next;
    if (!vmm_map_user_page(p->as, tcb_page_pa, tcb_user_va,
                        VM_PROT_USER | VM_PROT_READ | VM_PROT_WRITE))
        goto fail_kstack;

    vm_region_t tcb_region = {
        .vaddr_start = tcb_user_va,
        .size = PAGE_SIZE,
        .prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_ANON,
        .flags = VM_FLAG_NONE,
    };
    if (!vmm_add_region(p->as, &tcb_region))
        goto fail_kstack;

    p->tcb_page_va = tcb_user_va; /* user-visible VA */

    /* Advance bump pointer to reserve the TCB page */
    p->mmap_va_next += PAGE_SIZE;

    /* Initialize the page via the kernel alias (kernel VA). The
     * initial slot (slot 0) belongs to the main thread and should
     * point at the process's IPCX fixed VA. */
    tdata_t *tcb0 = (tdata_t *)PA_TO_VA(tcb_page_pa);
    tcb0->ipc_buf = (void *)USER_IPC_BUF_VA;
    tcb0->tid = t->tid;
    /* Point the thread's thread-info VA at the first slot in the user TCB page */
    t->thread_info_va = p->tcb_page_va;
    

    // slot 0 is reserved for nametable endpoint when available
    handle_entry_t *slot0 = handle_vec_get(&p->handle_table, 0);
    if (!slot0)
        goto fail_kstack;

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

    /* `device_va_next` and `mmap_va_next` were initialized earlier. */
    p->parent_pid = 0;
    t->priority = 1;
    t->time_slice = 5;
    t->ticks_remaining = t->time_slice;
    p->flags = 0;

    if (name) {
        const char *short_name = name;
        for (const char *p = name; *p; p++) {
            if (*p == '/')
                short_name = p + 1;
        }
        strncpy(p->name, short_name, sizeof(p->name) - 1);
    }

    zpid_t start = next_pid % MAX_PROCESSES;
    uint32_t slot = start;
    do {
        if (process_table[slot] == NULL)
            break;
        next_pid++;
        slot = next_pid % MAX_PROCESSES;
    } while (slot != start);

    if (process_table[slot] != NULL)
        goto fail_kstack;

    p->pid = next_pid++;
    process_table[slot] = p;
    tcb0->pid = p->pid;
    return p;

fail_kstack:
    if (p->as)
        arch_mmu_free_user_pages(p->as);
    as_destroy(p->as);
fail_handles:
    if (nametable_endpoint) {
        handle_entry_t *maybe_slot0 = handle_vec_get(&p->handle_table, 0);
        if (maybe_slot0 && maybe_slot0->type == HANDLE_ENDPOINT && maybe_slot0->ep == nametable_endpoint) {
            if (nametable_endpoint->ref_count > 0)
                nametable_endpoint->ref_count--;
        }
    }
    handle_vec_destroy(&p->handle_table);
    thread_destroy(t);
fail_process:
    kfree(p);
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
    rc->caller_tid = 0;
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
        rc->caller_tid = 0;
        rc->holder_pid = 0;
        rc->holder_slot = 0;
        kfree_reply_cap(rc);
    }
}



process_t *process_find_by_pid(zpid_t pid)
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

process_t *process_find_child_by_pid(process_t *parent, zpid_t pid)
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
        if (child->thread->state == ZOMBIE)
            return child;
        node = node->next;
    }

    return NULL;
}

void process_wake_joiners(tid_t tid, int32_t exit_status)
{
    for (uint32_t slot = 0; slot < MAX_PROCESSES; slot++) {
        process_t *joiner = process_table[slot];
        if (!joiner || joiner->waiting_for_tid != tid)
            continue;

        joiner->waiting_for_tid = 0;
        if (joiner->thread) {
            joiner->thread->wake_reason = WAKE_IPC;
            joiner->thread->state = READY;
            if (joiner->thread->trap_frame)
                (*arch_reg(joiner->thread->trap_frame, 0)) = (uint32_t)exit_status;
            sched_add(joiner->thread);
        }
    }
}

void process_kill(process_t *p, const int exit_status) {
    if (!p)
        return;

    if (p->flags & (PROC_FLAG_INIT | PROC_FLAG_DEVMGR)) {
        panic("Attempted to kill critical process");
    }

    list_node_t *thread_node = p->threads.node.next;
    while (thread_node != &p->threads.node) {
        list_node_t *next_thread = thread_node->next;
        thread_t *thread = container_of(thread_node, thread_t, process_node);
        thread->exit_status = exit_status;

        // remove from run queue / sleep queue / IPC queue
        if (thread->node.prev && thread->node.next)
            list_remove(&thread->node);
        if (thread->timeout_node.prev && thread->timeout_node.next)
            list_remove(&thread->timeout_node);

        thread_kill(thread);  // state = ZOMBIE
        thread_node = next_thread;
    }
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
                    thread_t *thread = container_of(n, thread_t, node);
                    thread->ipc_state = IPC_NONE;
                    thread->blocked_endpoint = NULL;
                    thread->wake_reason = WAKE_IPC;
                    if (thread->trap_frame)
                        (*arch_reg(thread->trap_frame, 0)) = ERR_DEAD;
                    thread->state = READY;
                    sched_add(thread);
                }
                while (!list_empty(&ep->receiver_queue)) {
                    list_node_t *n = list_pop_front(&ep->receiver_queue);
                    thread_wait_slot_t *slot = container_of(n, thread_wait_slot_t, node);
                    thread_t *thread = slot->owner;
                    if (thread->recvany_ep_wait_active) {
                        thread_recvany_clear_waits(thread);
                        thread_recvany_clear_ep_waits(thread);
                    } else {
                        thread->ipc_state = IPC_NONE;
                        thread->blocked_endpoint = NULL;
                    }
                    if (thread->wake_tick != 0 && thread->timeout_node.prev && thread->timeout_node.next)
                        list_remove(&thread->timeout_node);
                    thread->wake_tick = 0;
                    thread->wake_reason = WAKE_IPC;
                    if (thread->trap_frame)
                        (*arch_reg(thread->trap_frame, 0)) = ERR_DEAD;
                    thread->state = READY;
                    sched_add(thread);
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
            thread_t *caller_thread = thread_find_by_tid(rc ? rc->caller_tid : 0);

            if (caller_thread && caller_thread->ipc_state == IPC_WAITING) {
                caller_thread->ipc_state = IPC_NONE;
                caller_thread->blocked_endpoint = NULL;
                caller_thread->wake_reason = WAKE_IPC;
                if (caller_thread->trap_frame)
                    (*arch_reg(caller_thread->trap_frame, 0)) = ERR_DEAD;
                caller_thread->state = READY;
                sched_add(caller_thread);
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
                    thread_wait_slot_t *slot = container_of(n, thread_wait_slot_t, node);
                    thread_t *thread = slot->owner;
                    if (thread->trap_frame)
                        (*arch_reg(thread->trap_frame, 0)) = ERR_DEAD;
                    thread_recvany_clear_waits(thread);
                    if (thread->wake_tick != 0 && thread->timeout_node.prev && thread->timeout_node.next)
                        list_remove(&thread->timeout_node);
                    thread->wake_tick = 0;
                    thread->state = READY;
                    thread->wake_reason = WAKE_IPC;
                    thread->blocked_endpoint = NULL;
                    thread->ipc_state = IPC_NONE;
                    sched_add(thread);
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

    process_t *parent = process_find_by_pid(p->parent_pid);
    if (parent && parent->thread && parent->thread->state == BLOCKED
              && (parent->waiting_for == p->pid || parent->waiting_for == UINT32_MAX)) {
        parent->thread->state = READY;
        parent->waiting_for = 0;
        sched_add(parent->thread);
    } else {
        sched_defer_destroy(p);
    }
}

void process_destroy(process_t *p)
{
    if (!p)
        return;

    //KDEBUG("process_destroy: pid=%u current_tid=%u", p->pid, current_thread ? current_thread->tid : 0);

    irq_release_all(p);
    if (p->node.prev && p->node.next)
        list_remove(&p->node);
    if (p->sibling_node.prev && p->sibling_node.next)
        list_remove(&p->sibling_node);
    if (p->destroy_node.prev && p->destroy_node.next)
        list_remove(&p->destroy_node);
    if (p->timeout_node.prev && p->timeout_node.next)
        list_remove(&p->timeout_node);
    while (!list_empty(&p->threads)) {
        list_node_t *node = p->threads.node.next;
        thread_t *thread = container_of(node, thread_t, process_node);
        thread_destroy(thread);
    }
    if (p->as)
    {
        arch_mmu_free_user_pages(p->as);
        as_destroy(p->as);
    }
    handle_vec_destroy(&p->handle_table);
    process_table[p->pid % MAX_PROCESSES] = NULL;
    kfree(p);
}
