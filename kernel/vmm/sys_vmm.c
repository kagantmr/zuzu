#include "sys_vmm.h"
#include "kernel/syscall/syscall.h"
#include "kernel/sched/sched.h"
#include "arch/arm/mmu/mmu.h"
#include "kernel/mm/pmm.h"
#include "kernel/layout.h"
#include <mem.h>

#define LOG_FMT(fmt) "(syscall_vmm) " fmt
#include "core/log.h"
#include "kernel/mm/alloc.h"

extern process_t *current_process;
extern phys_region_t phys_region;

void memmap(exception_frame_t *frame)
{
    uint32_t addr_hint = frame->r[0];
    uint32_t size = frame->r[1];
    uint32_t prot = frame->r[2];
    if (size == 0)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (size > 1024 * 1024 * 32)
    {
        // 32mb is half of the recommended kernel mem anyway
        frame->r[0] = ERR_NOMEM;
        return;
    }
    if (size % PAGE_SIZE)
    {
        // Needs VMM/PMM alignment
        frame->r[0] = ERR_BADARG;
        return;
    }

    if (addr_hint != 0)
    {
        if (!validate_user_ptr(addr_hint, size))
        {
            frame->r[0] = ERR_BADARG;
            return;
        }
    }

    // 1. Pick a VA
    uintptr_t va;
    if (addr_hint != 0)
    {
        va = addr_hint; // already validated above
    }
    else
    {
        va = current_process->mmap_va_next;
    }

    // 2. Bump the cursor
    if (current_process->mmap_va_next > va + size) // check for overflow
    {
        frame->r[0] = ERR_BADARG;
        return;
    }
    current_process->mmap_va_next += size;

    // 3. Register the region — no physical pages allocated yet
    vm_region_t region = {
        .vaddr_start = va,
        .paddr_start = 0, // filled in at fault time, page by page
        .size = size,
        .prot = prot | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_ANON,
        .flags = VM_FLAG_NONE,
    };

    if (!vmm_add_region(current_process->as, &region))
    {
        current_process->mmap_va_next -= size; // roll back cursor on failure
        frame->r[0] = ERR_NOMEM;
        return;
    }

    frame->r[0] = va;
}

void memunmap(exception_frame_t *frame)
{
    const uintptr_t va = (uintptr_t)frame->r[0];
    size_t size = (size_t)frame->r[1];

    // Basic validation
    if (!validate_user_ptr(va, size))
    {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (size == 0 || size % PAGE_SIZE != 0)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    addrspace_t *as = current_process->as;

    // Find the region, it must be an exact match to prevent partial-unmap attacks
    vm_region_t *found = NULL;
    for (size_t i = 0; i < as->region_count; i++)
    {
        if (as->regions[i].vaddr_start == va && as->regions[i].size == size)
        {
            found = &as->regions[i];
            break;
        }
    }
    if (!found)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    // If we own the pages, free them back to PMM before unmapping
    if (found->owner == VM_OWNER_ANON)
    {
        // Walk page table to find which physical pages are actually backed
        // (demand paging means not every page in the region may be mapped)
        for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE)
        {
            uintptr_t pa = arch_mmu_translate(as->ttbr0_pa, va + offset);
            if (pa != 0)
            {
                pmm_free_page(pa);
            }
        }
    }
    else if (found->owner == VM_OWNER_SHARED)
    {
        bool found_handle = false;
        // If it's shared memory, we need to decrement the ref count and free if it hits 0
        for (uint32_t i = 0; i < current_process->handle_table.cap; i++)
        {
            handle_entry_t *entry = handle_vec_get(&current_process->handle_table, i);
            if (!entry)
                break;

            if (entry->type == HANDLE_SHMEM && entry->mapped_va == va)
            {
                shmem_t *shm_obj = entry->shm;
                shm_obj->ref_count--;
                if (shm_obj->ref_count == 0)
                {
                    for (size_t j = 0; j < shm_obj->page_count; j++)
                        pmm_free_page(shm_obj->page_addrs[j]);
                    kfree(shm_obj->page_addrs);
                    kfree(shm_obj);
                }
                entry->shm = NULL;
                entry->mapped_va = 0;
                entry->grantable = false;
                entry->type = HANDLE_FREE;
                found_handle = true;
                break;
            }
        }
        if (!found_handle)
        {
            KWARN("memunmap: shared region @ 0x%08x has no matching handle", va);
        }
    }

    // Remove from region list and unmap page table entries
    if (!vmm_remove_region(as, va, size))
    {
        frame->r[0] = ERR_NOMEM;
        return;
    }

    frame->r[0] = 0;
}

void memshare(exception_frame_t *frame)
{
    const size_t size = (size_t)frame->r[0];
    if (size == 0)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (size > 1024 * 1024 * 32)
    {
        frame->r[0] = ERR_NOMEM;
        return;
    }
    if (size % PAGE_SIZE)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    const size_t page_count = size / PAGE_SIZE;
    uintptr_t *page_arr = kmalloc(sizeof(uintptr_t) * page_count);
    if (!page_arr)
    {
        frame->r[0] = ERR_NOMEM;
        return;
    }

    const size_t returned_pages = pmm_alloc_pages_scattered(page_count, page_arr);
    if (page_count != returned_pages)
    {
        for (size_t i = 0; i < returned_pages; i++)
            pmm_free_page(page_arr[i]);
        kfree(page_arr);
        frame->r[0] = ERR_NOMEM;
        return;
    }

    shmem_t *shmem_obj = kmalloc(sizeof(shmem_t));
    if (!shmem_obj)
    {
        for (size_t i = 0; i < page_count; i++)
            pmm_free_page(page_arr[i]);
        kfree(page_arr);
        frame->r[0] = ERR_NOMEM;
        return;
    }
    shmem_obj->page_count = page_count;
    shmem_obj->ref_count = 1;
    shmem_obj->page_addrs = page_arr;

    // find free handle slot
    int handle = handle_vec_find_free(&current_process->handle_table);
    if (handle < 0)
    {
        for (size_t i = 0; i < page_count; i++)
            pmm_free_page(page_arr[i]);
        kfree(page_arr);
        kfree(shmem_obj);
        frame->r[0] = ERR_NOMEM;
        return;
    }

    // pick VA base and bump cursor once
    if (current_process->mmap_va_next > UINTPTR_MAX - size) // check for overflow
    {
        for (size_t i = 0; i < page_count; i++)
            pmm_free_page(page_arr[i]);
        kfree(page_arr);
        kfree(shmem_obj);
        frame->r[0] = ERR_BADARG;
        return;
    }
    const uintptr_t va_base = current_process->mmap_va_next;
    current_process->mmap_va_next += size;

    vm_region_t region = {
        .vaddr_start = va_base,
        .size = shmem_obj->page_count * PAGE_SIZE,
        .prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_SHARED,
        .flags = VM_FLAG_NONE,
    };
    if (!vmm_add_region(current_process->as, &region))
    {
        current_process->mmap_va_next -= size;
        for (size_t i = 0; i < page_count; i++)
            pmm_free_page(page_arr[i]);
        kfree(page_arr);
        kfree(shmem_obj);
        frame->r[0] = ERR_NOMEM;
        return;
    }
    for (size_t j = 0; j < page_count; j++)
    {
        if (!vmm_map_range(current_process->as, va_base + j * PAGE_SIZE, page_arr[j],
                           PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
                           VM_MEM_NORMAL, VM_OWNER_SHARED, VM_FLAG_NONE))
        {
            // unmap what we already mapped
            for (size_t k = 0; k < j; k++)
                vmm_unmap_range(current_process->as, va_base + k * PAGE_SIZE, PAGE_SIZE);
            current_process->mmap_va_next -= size;
            for (size_t i = 0; i < page_count; i++)
                pmm_free_page(page_arr[i]);
            kfree(page_arr);
            kfree(shmem_obj);
            frame->r[0] = ERR_NOMEM;
            return;
        }
    }

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, (uint32_t)handle);
    if (!entry)
    {
        vmm_remove_region(current_process->as, va_base, size);
        for (size_t j = 0; j < page_count; j++)
            vmm_unmap_range(current_process->as, va_base + j * PAGE_SIZE, PAGE_SIZE);
        current_process->mmap_va_next -= size;
        for (size_t i = 0; i < page_count; i++)
            pmm_free_page(page_arr[i]);
        kfree(page_arr);
        kfree(shmem_obj);
        frame->r[0] = ERR_NOMEM;
        return;
    }

    entry->mapped_va = va_base;
    entry->shm = shmem_obj;
    entry->type = HANDLE_SHMEM;
    entry->grantable = true;

    frame->r[0] = (uint32_t)handle;
    frame->r[1] = (uint32_t)va_base;
}

void attach(exception_frame_t *frame)
{
    const uint32_t handle_idx = frame->r[0];

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle_idx);
    if (!entry)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (entry->type != HANDLE_SHMEM)
    {
        frame->r[0] = ERR_BADFORM;
        return;
    }
    shmem_t *shm_obj = entry->shm;
    const uintptr_t va_base = current_process->mmap_va_next;
    current_process->mmap_va_next += shm_obj->page_count * PAGE_SIZE;

    vm_region_t region = {
        .vaddr_start = va_base,
        .size = shm_obj->page_count * PAGE_SIZE,
        .prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_SHARED,
        .flags = VM_FLAG_NONE,
    };
    if (!vmm_add_region(current_process->as, &region))
    {
        current_process->mmap_va_next -= shm_obj->page_count * PAGE_SIZE;
        for (size_t i = 0; i < shm_obj->page_count; i++)
            pmm_free_page(shm_obj->page_addrs[i]);
        kfree(shm_obj->page_addrs);
        kfree(shm_obj);
        frame->r[0] = ERR_NOMEM;
        return;
    }
    for (size_t j = 0; j < shm_obj->page_count; j++)
    {
        if (!vmm_map_range(current_process->as, va_base + j * PAGE_SIZE, shm_obj->page_addrs[j],
                           PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
                           VM_MEM_NORMAL, VM_OWNER_SHARED, VM_FLAG_NONE))
        {
            // unmap what we already mapped
            for (size_t k = 0; k < j; k++)
                vmm_unmap_range(current_process->as, va_base + k * PAGE_SIZE, PAGE_SIZE);
            current_process->mmap_va_next -= shm_obj->page_count * PAGE_SIZE;
            frame->r[0] = ERR_NOMEM;
            return;
        }
    }

    entry->mapped_va = va_base;
    shm_obj->ref_count++;
    frame->r[0] = va_base;
}

void sys_pmm_getfree(exception_frame_t *frame)
{
    extern pmm_state_t pmm_state;
    frame->r[0] = (uint32_t)pmm_state.free_pages;
}

void detach(exception_frame_t *frame)
{
    uint32_t handle = frame->r[0];

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle);
    if (!entry)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (entry->type != HANDLE_SHMEM)
    {
        frame->r[0] = ERR_BADFORM;
        return;
    }

    shmem_t *shm = entry->shm;
    const uintptr_t va = entry->mapped_va;
    vmm_remove_region(current_process->as, va, shm->page_count * PAGE_SIZE);
    for (size_t j = 0; j < shm->page_count; j++)
        vmm_unmap_range(current_process->as, va + j * PAGE_SIZE, PAGE_SIZE);
    shm->ref_count--;
    if (shm->ref_count == 0)
    {
        for (size_t j = 0; j < shm->page_count; j++)
            pmm_free_page(shm->page_addrs[j]);
        kfree(shm->page_addrs);
        kfree(shm);
    }
    entry->shm = NULL;
    entry->mapped_va = 0;
    entry->grantable = false;
    entry->type = HANDLE_FREE;

    frame->r[0] = 0;
}