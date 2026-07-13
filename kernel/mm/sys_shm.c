#include "sys_shm.h"

#include "kernel/sched/sched.h"
#include <arch/cache.h>
#include <arch/mmu.h>
#include <mem.h>

#include "core/log.h"
#include "kernel/layout.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"

extern thread_t *current_thread;

void shm_create(arch_regs_t *frame)
{
    const size_t size = align_up((size_t)(*arch_reg(frame, 0)), PAGE_SIZE);
    if (size == 0)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }
    if (size > 1024 * 1024 * 32)
    {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }
    const size_t page_count = size / PAGE_SIZE;
    paddr_t *page_arr = kmalloc(sizeof(paddr_t) * page_count);
    if (!page_arr)
    {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }
    memset(page_arr, 0, sizeof(paddr_t) * page_count);

    shmem_t *shmem_obj = kmalloc(sizeof(shmem_t));
    if (!shmem_obj)
    {
        kfree(page_arr);
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }
    shmem_obj->page_count = page_count;
    shmem_obj->ref_count = 1;
    shmem_obj->page_addrs = page_arr;

    int handle = handle_vec_find_free(&current_thread->owner_process->handle_table);
    if (handle < 0)
    {
        kfree(page_arr);
        kfree(shmem_obj);
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, (uint32_t)handle);
    if (!entry)
    {
        kfree(page_arr);
        kfree(shmem_obj);
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }

    entry->mapped_va = 0;
    entry->shm = shmem_obj;
    entry->type = HANDLE_SHMEM;
    entry->grantable = true;

    (*arch_reg(frame, 0)) = (handle_t)handle;
}

void attach(arch_regs_t *frame)
{
    const handle_t handle_idx = (*arch_reg(frame, 0));

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle_idx);
    if (!entry)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }
    if (entry->type != HANDLE_SHMEM)
    {
        (*arch_reg(frame, 0)) = ERR_MALFORMED;
        return;
    }

    shmem_t *shm_obj = entry->shm;
    if (!shm_obj)
    {
        (*arch_reg(frame, 0)) = ERR_MALFORMED;
        return;
    }
    const size_t size = shm_obj->page_count * PAGE_SIZE;
    if (current_thread->owner_process->mmap_va_next > UINTPTR_MAX - size)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    const vaddr_t va_base = current_thread->owner_process->mmap_va_next;
    current_thread->owner_process->mmap_va_next += size;

    vm_region_t region = {
        .vaddr_start = va_base,
        .size = size,
        .prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_SHARED,
        .backing = shm_obj,
        .flags = VM_FLAG_NONE,
    };
    if (!vmm_add_region(current_thread->owner_process->as, &region))
    {
        current_thread->owner_process->mmap_va_next -= size;
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }
    entry->mapped_va = va_base;
    shm_obj->ref_count++;
    (*arch_reg(frame, 0)) = va_base;
}

void detach(arch_regs_t *frame)
{
    handle_t handle = (*arch_reg(frame, 0));

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle);
    if (!entry)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }
    if (entry->type != HANDLE_SHMEM)
    {
        (*arch_reg(frame, 0)) = ERR_MALFORMED;
        return;
    }

    shmem_t *shm = entry->shm;
    const vaddr_t va = entry->mapped_va;
    vmm_remove_region(current_thread->owner_process->as, va, shm->page_count * PAGE_SIZE);
    shm->ref_count--;
    if (shm->ref_count == 0)
    {
        for (size_t j = 0; j < shm->page_count; j++)
            if (shm->page_addrs[j] != 0)
                pmm_free_page(shm->page_addrs[j]);
        kfree(shm->page_addrs);
        kfree(shm);
    }
    entry->shm = NULL;
    entry->mapped_va = 0;
    entry->grantable = false;
    entry->type = HANDLE_FREE;

    (*arch_reg(frame, 0)) = 0;
}