#include "sys_mm.h"
#include "kernel/syscall/syscall.h"
#include "kernel/sched/sched.h"
#include <arch/mmu.h>
#include <arch/cache.h>
#include "core/panic.h"
#include "kernel/mm/pmm.h"
#include "kernel/layout.h"
#include <zuzu/spawn_args.h>
#include <mem.h>

#define LOG_FMT(fmt) "(syscall_mm) " fmt
#include "core/log.h"
#include "kernel/mm/alloc.h"

extern thread_t *current_thread;

/**
 * Helper for memmap to map anonymous memory.
 * Adapted from zuzu v0.1.5-alpha version
 */
static int32_t memmap_anon(process_t *p, vaddr_t hint, size_t size, vm_prot_t prot, vaddr_t *out)
{
    if (size == 0)
        return ERR_BADARG;

    // Do not enforce W^X here, that is memmap's job

    if (size > 1024 * 1024 * 32)
        return ERR_OVERFLOW; // 32mb static cap; half the recommended kernel mem

    if (size % PAGE_SIZE)
        return ERR_BADARG; // Needs page/frame alignment

    if (hint != 0)
    {
        if (!validate_user_ptr(hint, size))
            return ERR_BADARG;
    }

    // 1. Pick a VA
    vaddr_t va;
    if (hint != 0)
        va = hint; // already validated above
    else
        va = p->mmap_va_next;

    if (va >= USER_VA_TOP)
        return ERR_NOMEM; // user VA space exhausted
    // 2. Bump the cursor
    if (size > USER_VA_TOP - va) // no contiguous VA left
        return ERR_NOMEM;

    if (hint == 0)
        p->mmap_va_next += size;

    vm_region_t region = {
        .vaddr_start = va,
        .paddr_start = 0, // filled in at fault time, page by page
        .size = size,
        .prot = prot | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_ANON,
        .flags = VM_FLAG_NONE,
    };

    if (!vmm_add_region(p->as, &region))
    {
        if (hint == 0)
            p->mmap_va_next -= size; // roll back cursor on failure
        return ERR_NOMEM;
    }

    *out = va;
    return ZUZU_OK;
}

/**
 * Helper for memmap to map shared memory.
 * Adapted from zuzu v0.1.5-alpha version
 */
static int32_t memmap_shm(process_t *p, handle_entry_t *e, vm_prot_t prot, vaddr_t *out)
{

    if (e->mapped_va != 0)
        return ERR_BUSY;

    // pick VA base and bump cursor once
    shmem_t *shmem_obj = e->shm;

    if (!shmem_obj)
        return ERR_BADHANDLE;

    size_t size = shmem_obj->page_count * PAGE_SIZE;

    if ((p->mmap_va_next > USER_VA_TOP - size) || (size > USER_VA_TOP - p->mmap_va_next)) // user VA space exhausted
        return ERR_NOMEM;

    const vaddr_t va_base = p->mmap_va_next;
    p->mmap_va_next += size;

    vm_region_t region = {
        .vaddr_start = va_base,
        .size = size,
        .prot = prot | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_SHARED,
        .backing = shmem_obj,
        .flags = VM_FLAG_NONE};
    if (!vmm_add_region(p->as, &region))
    {
        p->mmap_va_next -= size;
        return ERR_NOMEM; // OOM
    }

    e->mapped_va = va_base;
    *out = va_base;
    return ZUZU_OK;
}

/**
 * Helper for memmap to map memory-mapped I/O (MMIO) ranges.
 * Adapted from zuzu v0.1.5-alpha version
 */
static int32_t memmap_dev(process_t *p, handle_entry_t *e, vm_prot_t prot, vaddr_t *out)
{

    if (!e)
        return ERR_BADHANDLE;

    device_cap_t *cap = e->dev;
    if (!cap)
        return ERR_BADHANDLE;

    if (e->mapped_va)
        return ERR_BUSY;

    size_t size_aligned = align_up(cap->size, PAGE_SIZE);
    vaddr_t user_va = p->device_va_next;

    // Device mappings are carved from device_va_next; bound-check that cursor.
    if (user_va >= USER_DEVICE_LIMIT || size_aligned > USER_DEVICE_LIMIT - user_va)
    {
        return ERR_NOMEM;
    }

    if (!vmm_map_range(p->as, user_va, cap->phys_base, size_aligned,
                       prot | VM_PROT_USER,
                       VM_MEM_DEVICE, VM_OWNER_NONE, VM_FLAG_NONE))
        return ERR_NOMEM;

    if (!vmm_add_region(p->as, &(vm_region_t){
                                   .vaddr_start = user_va,
                                   .paddr_start = cap->phys_base,
                                   .backing = cap,
                                   .size = size_aligned,
                                   .prot = prot | VM_PROT_USER,
                                   .memtype = VM_MEM_DEVICE,
                                   .owner = VM_OWNER_NONE,
                                   .flags = VM_FLAG_NONE,
                               }))
    {
        vmm_unmap_range(p->as, user_va, size_aligned);
        return ERR_NOMEM;
    }

    // flush TLB for this VA
    arch_mmu_flush_tlb_va(user_va);
    arch_mmu_barrier();

    p->device_va_next += size_aligned;
    e->mapped_va = user_va;

    *out = user_va;
    return ZUZU_OK;
}

void sys_memmap(arch_regs_t *frame)
{
    process_t *p = current_thread->owner_process;
    handle_t handle = (handle_t)(*arch_reg(frame, 0));
    size_t size = (size_t)(*arch_reg(frame, 1));
    vm_prot_t prot = (vm_prot_t)(*arch_reg(frame, 2));
    uint32_t flags = (*arch_reg(frame, 3));

    if (flags != 0) { *arch_reg(frame, 0) = ERR_BADARG; return;}
    if (prot & ~(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXEC))  { *arch_reg(frame, 0) = ERR_BADARG; return;}   /* rejects VM_PROT_USER */
    if ((prot & VM_PROT_WRITE) && (prot & VM_PROT_EXEC))  { *arch_reg(frame, 0) = ERR_BADARG; return;}

    vaddr_t va = 0;
    err_t rc;   

    if (handle == HANDLE_ANON)
    {
        rc = memmap_anon(p, 0, size, prot, &va); /* hint dies at step D */
    }
    else
    {
        handle_entry_t *e = handle_vec_get(&p->handle_table, handle);
        if (!e)
        {
            *arch_reg(frame, 0) = ERR_BADHANDLE;
            return;
        }
        switch (e->type)
        {
        case HANDLE_DEVICE:
        {
            if (size != 0) {
                *arch_reg(frame, 0) = ERR_BADARG;
                return;
            }
            if (prot & VM_PROT_EXEC) {
                *arch_reg(frame, 0) = ERR_BADARG;
                return;
            }
            rc = memmap_dev(p, e, prot, &va);
        }
        break;

        case HANDLE_SHMEM:
        {
            if (size != 0) {
                *arch_reg(frame, 0) = ERR_BADARG;
                return;
            }
            rc = memmap_shm(p, e, prot, &va);
    
        }
        break;
        default:
        {
            rc = ERR_BADTYPE;
        } break;
        }
    }

    (*arch_reg(frame, 0)) = (rc == ZUZU_OK) ? (uint32_t)va : (uint32_t)rc;
    return;
}

void sys_memunmap(arch_regs_t *frame)
{
        const vaddr_t va = (vaddr_t)(*arch_reg(frame, 0));

        addrspace_t *as = current_thread->owner_process->as;

        // Find the region, it must be an exact match to prevent partial-unmap attacks
        vm_region_t *found = NULL;
        for (uint32_t i = 0; i < as->regions.len; i++) {
            vm_region_t *r = vm_region_vec_get(&as->regions, i);
            if (r && r->vaddr_start == va) { found = r; break; }   /* base match only */
        }
        if (!found) { (*arch_reg(frame, 0)) = ERR_BADARG; return; }
        if (found->flags & VM_FLAG_PINNED) { (*arch_reg(frame, 0)) = ERR_NOPERM; return; }

        size_t size = found->size;

        switch(found->owner) {
            // If we own the pages, free them back to PMM before unmapping
            case VM_OWNER_ANON:
            {
                // Walk page table to find which physical pages are actually backed
                // (demand paging means not every page in the region may be mapped)
                for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE)
                {
                    uintptr_t pa = arch_mmu_translate(as->ttbr_pa, va + offset);
                    if (pa != 0) pmm_free_page(pa);
                }
            } break;
            case VM_OWNER_SHARED:
            case VM_OWNER_NONE:
            {
                // Shared or device mapping: clear the owning handle's mapped_va so memmap can remap it
                bool found_handle = false;
                for (uint32_t i = 0; i < current_thread->owner_process->handle_table.cap; i++)
                {
                    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, i);
                    if (!entry || entry->mapped_va != va ||
                        (entry->type != HANDLE_SHMEM && entry->type != HANDLE_DEVICE))
                        continue;

                    entry->mapped_va = 0;
                    found_handle = true;
                    break;
                }
                if (!found_handle)
                {
                    KWARN("sys_memunmap: region @ 0x%08x has no matching handle", va);
                }
            } break;
            default:
                break;
        }
        // Remove from region list and unmap page table entries
        if (!vmm_remove_region(as, va, size))
        {
            panic("sys_memunmap: found region @ 0x%08X (size=%u) but could not remove it",
                  (uint32_t)va, (unsigned)size);
            __builtin_unreachable();
        }
    
        (*arch_reg(frame, 0)) = 0;
}

void sys_asinject(arch_regs_t *frame)
{
        if (!(current_thread->owner_process->flags & PROC_FLAG_INIT))
        {
            {
            (*arch_reg(frame, 0)) = ERR_NOPERM;
            return;
        }
        }

        asinject_args_t *args = (asinject_args_t *)(*arch_reg(frame, 0));
        if (!validate_user_ptr((uintptr_t)args, sizeof(asinject_args_t)))
        {
            {
            (*arch_reg(frame, 0)) = ERR_BADPTR;
            return;
        }
        }

        asinject_args_t kargs;
        if (!copy_from_user(&kargs, args, sizeof(asinject_args_t)))
        {
            {
            (*arch_reg(frame, 0)) = ERR_BADPTR;
            return;
        }
        }

        if (kargs.size < sizeof(asinject_args_t))
        {
            {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }
        }

        handle_entry_t *handle = handle_vec_get(&current_thread->owner_process->handle_table, kargs.task_handle);
        if (!handle)
        {
            (*arch_reg(frame, 0)) = ERR_BADHANDLE;
            return;
        }
        if (handle->type != HANDLE_TASK)
        {
            (*arch_reg(frame, 0)) = ERR_BADTYPE;
            return;
        }

        process_t *target = handle->task;

        if (!target)
        {
            (*arch_reg(frame, 0)) = ERR_BADHANDLE;
            return;
        }
        if (target->thread->state != FROZEN)
        {
            (*arch_reg(frame, 0)) = ERR_BUSY;
            return;
        }

        if (!kargs.src_buf || kargs.len == 0)
        {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }
        if (!validate_user_ptr((uintptr_t)kargs.src_buf, kargs.len))
        {
            (*arch_reg(frame, 0)) = ERR_BADPTR;
            return;
        }

        if ((kargs.prot & VM_PROT_WRITE) && (kargs.prot & VM_PROT_EXEC))
        {
            {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }
        }

        if (kargs.dst_va % PAGE_SIZE != 0 ||
            kargs.dst_va >= USER_VA_TOP ||
            kargs.len > USER_VA_TOP - kargs.dst_va)
        {
            {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }
        }

        size_t page_count = (kargs.len + PAGE_SIZE - 1) / PAGE_SIZE;

        /* If the destination lies entirely inside an existing anon region
         * (e.g. the demand-paged stack reserve), fill pages in place instead
         * of creating a new region. Injected prot must not exceed the
         * region's prot. */
        vm_region_t *enclosing = NULL;
        for (uint32_t i = 0; i < target->as->regions.len; i++)
        {
            vm_region_t *r = vm_region_vec_get(&target->as->regions, i);
            /* dst must lie inside the region before computing the remaining
             * space, or the unsigned subtraction below wraps for regions
             * that end before dst_va. */
            if (kargs.dst_va >= r->vaddr_start &&
                kargs.dst_va - r->vaddr_start < r->size &&
                page_count * PAGE_SIZE <= r->size - (kargs.dst_va - r->vaddr_start))
            {
                enclosing = r;
                break;
            }
        }
        if (enclosing)
        {
            if ((enclosing->flags & VM_FLAG_GUARD) ||
                enclosing->owner != VM_OWNER_ANON ||
                enclosing->memtype != VM_MEM_NORMAL ||
                ((kargs.prot | VM_PROT_USER) & ~enclosing->prot))
            {
                {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }
            }
        }

        vaddr_t *page_addrs = kmalloc(page_count * sizeof(vaddr_t));
        if (!page_addrs)
        {
            {
            (*arch_reg(frame, 0)) = ERR_NOMEM;
            return;
        }
        }
        memset(page_addrs, 0, page_count * sizeof(vaddr_t));

        for (size_t i = 0; i < page_count; i++)
        {
            vaddr_t dst_page = kargs.dst_va + i * PAGE_SIZE;

            /* Inside an existing region a page may already be faulted in;
             * write into it instead of remapping. page_addrs[] tracks only
             * pages we allocated ourselves, for rollback. */
            paddr_t page = enclosing ? arch_mmu_translate(target->as->ttbr_pa, dst_page) : 0;
            bool fresh = (page == 0);
            if (fresh)
            {
                page = pmm_alloc_page();
                if (!page)
                {
                    goto rollback_nomem;
                }
                page_addrs[i] = page;
            }

            // copy from user buffer to the new page
            size_t offset = i * PAGE_SIZE;
            size_t bytes_to_copy = kargs.len - offset;
            if (bytes_to_copy > PAGE_SIZE)
                bytes_to_copy = PAGE_SIZE;

            if (!copy_from_user((void *)PA_TO_VA(page),
                                (const void *)((uintptr_t)kargs.src_buf + offset),
                                bytes_to_copy))
            {
                goto rollback_badarg;
            }

            if (fresh && bytes_to_copy < PAGE_SIZE)
            {
                memset((void *)(PA_TO_VA(page) + bytes_to_copy), 0,
                       PAGE_SIZE - bytes_to_copy);
            }

            if (fresh && !vmm_map_user_page(target->as, page, dst_page, kargs.prot))
            {
                pmm_free_page(page);
                page_addrs[i] = 0;
                goto rollback_nomem;
            }

            if (kargs.prot & VM_PROT_EXEC)
            {
                arch_cache_flush_code_range((vaddr_t)PA_TO_VA(page), PAGE_SIZE);
            }
        }

        if (!enclosing)
        {
            vm_region_t region = {
                .vaddr_start = kargs.dst_va,
                .size = page_count * PAGE_SIZE,
                .prot = kargs.prot | VM_PROT_USER,
                .memtype = VM_MEM_NORMAL,
                .owner = VM_OWNER_ANON,
                .flags = VM_FLAG_NONE,
            };
            if (!vmm_add_region(target->as, &region))
                goto rollback_nomem;
        }

        kfree(page_addrs);

        (*arch_reg(frame, 0)) = 0;
        return;

    rollback_badarg:
        for (size_t j = 0; j < page_count; j++)
        {
            if (page_addrs[j])
            {
                vmm_unmap_range(target->as, kargs.dst_va + j * PAGE_SIZE, PAGE_SIZE);
                pmm_free_page(page_addrs[j]);
            }
        }
        kfree(page_addrs);
        {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }

    rollback_nomem:
        for (size_t j = 0; j < page_count; j++)
        {
            if (page_addrs[j])
            {
                vmm_unmap_range(target->as, kargs.dst_va + j * PAGE_SIZE, PAGE_SIZE);
                pmm_free_page(page_addrs[j]);
            }
        }
        kfree(page_addrs);
        {
            (*arch_reg(frame, 0)) = ERR_NOMEM;
            return;
        }
}

void sys_memprotect(arch_regs_t *frame)
{
        const uintptr_t va = (uintptr_t)(*arch_reg(frame, 0));
        const size_t size = (size_t)(*arch_reg(frame, 1));
        const vm_prot_t new_prot = (vm_prot_t)(*arch_reg(frame, 2));

        // Basic validation
        if (size == 0)
        {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }
        if (size % PAGE_SIZE != 0)
        {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }
        if (!validate_user_ptr(va, size))
        {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }
        if (new_prot & ~(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXEC)) {
            (*arch_reg(frame, 0)) = ERR_NOPERM;
            return;
        }

        // Enforce W^X policy
        if ((new_prot & VM_PROT_WRITE) && (new_prot & VM_PROT_EXEC))
        {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }

        // The region must exist; use vmm_protect_range to change its protections
        if (!vmm_protect_range(current_thread->owner_process->as, va, size, new_prot | VM_PROT_USER))
        {
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }

        (*arch_reg(frame, 0)) = 0;
}