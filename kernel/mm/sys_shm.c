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

void sys_shm_create(arch_regs_t *frame)
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
    /* ref_count counts live mappings, not handles: memmap/attach take a ref,
     * memunmap/detach drop it. Creation maps nothing, so it starts at 0. */
    shmem_obj->ref_count = 0;
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