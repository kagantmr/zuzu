#include "loader.h"
#include <mem.h>
#include <string.h>

#include "kernel/proc/kstack.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "arch/arm/mmu/mmu.h"
#include "arch/arm/include/cache.h"
#include "kernel/loader/elf.h"
#include "kernel/mm/alloc.h"   
#include "kernel/syspage.h"
#include "zuzu/ipcx.h"

extern uint32_t next_pid;
extern process_t *process_table[MAX_PROCESSES];

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

#define LOG_FMT(fmt) "(elf_loader) " fmt
#include "core/log.h"

process_t *process_create_from_elf(const void *elf_data, size_t elf_size, const char *name)
{
    uint32_t elf_entry = elf_validate(elf_data, elf_size); // validate ELF and get entry point (also validates that it's an ARM executable)
    if (!elf_entry)
        return NULL;

    // malloc a PCB and zero it out (could actually do this with a kcalloc?)
    process_t *process = kmalloc(sizeof(process_t));
    if (!process)
        return NULL;
    memset(process, 0, sizeof(process_t));

    if (!handle_vec_init(&process->handle_table))
        goto fail_process;

    // try creating as
    process->as = as_create(ADDRSPACE_USER);
    if (!process->as)
        goto fail_process;

    // try getting a kstack
    uintptr_t stack_top = kstack_alloc();
    if (!stack_top)
        goto fail_as;
    process->kernel_stack_top = stack_top;

    // magic logic removed

    // Reject ELF images with overlapping PT_LOAD ranges before allocating or mapping anything.
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

    // for each PT_LOAD segment, copy the data and map it (for now we just assume one segment that covers the whole file, but this should be easy to extend)
    for (int i = 0; i < elf_phdr_count(elf_data); i++)
    {
        Elf32_Phdr *ph = elf_phdr_get(elf_data, i);
        if (ph->p_type == PT_LOAD)
        {
            if (ph->p_offset + ph->p_filesz > elf_size)
            { // segment extends past end of file
                goto fail_kstack;
            }
            uint32_t pages_needed = (ph->p_memsz + PAGE_SIZE - 1) / PAGE_SIZE;
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
                        vmm_unmap_range(process->as, orphan_va, PAGE_SIZE);
                        pmm_free_page(segment_pages[j]);
                    }
                    kfree(segment_pages);
                    goto fail_kstack;
                }

                segment_pages[page] = page_pa;

                uintptr_t file_offset = page * PAGE_SIZE;
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

                uintptr_t va = ph->p_vaddr + page * PAGE_SIZE;
                if (!kmap_user_page(process->as, page_pa, va, prot))
                {
                    pmm_free_page(page_pa);
                    for (uint32_t j = 0; j < page; j++)
                    {
                        uintptr_t orphan_va = ph->p_vaddr + j * PAGE_SIZE;
                        vmm_unmap_range(process->as, orphan_va, PAGE_SIZE);
                        pmm_free_page(segment_pages[j]);
                    }
                    kfree(segment_pages);
                    goto fail_kstack;
                }

                cache_flush_code_range((uintptr_t)PA_TO_VA(page_pa), PAGE_SIZE);
            }

            kfree(segment_pages);

        }
    }

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

    if (!kmap_user_page(process->as, syspage_pa(), 0x1000, VM_PROT_READ))
        goto fail_kstack;

    process->ipc_buf_pa = pmm_alloc_page();
    if (!process->ipc_buf_pa)
        goto fail_kstack;
    if (!kmap_user_page(process->as, process->ipc_buf_pa, IPCX_BUF_VA, VM_PROT_READ | VM_PROT_WRITE))
    {
        pmm_free_page(process->ipc_buf_pa);
        goto fail_kstack;
    }
    vm_region_t guard = {
        .vaddr_start = 0x7FFFD000,
        .size        = PAGE_SIZE,
        .prot        = 0,
        .memtype     = VM_MEM_NORMAL,
        .owner       = VM_OWNER_NONE,
        .flags       = VM_FLAG_GUARD,
    };
    vmm_add_region(process->as, &guard);

    // write exception frame to the stack
    stack_top -= 17 * sizeof(uint32_t);
    uint32_t *exc_frame = (uint32_t *)stack_top;
    for (int i = 0; i < 13; i++)
        *(exc_frame++) = 0;          // r0-r12
    *(exc_frame++) = USR_SP;         // sp_usr
    *(exc_frame++) = 0;              // lr_usr
    *(exc_frame++) = elf_entry;      // return_pc
    *(exc_frame++) = 0x10;           // cpsr = USR mode

    // write cpu_context to stack
    stack_top -= sizeof(cpu_context_t);
    cpu_context_t *context = (cpu_context_t *)stack_top;
    memset(context, 0, sizeof(cpu_context_t));
    context->lr = (uint32_t)process_entry_trampoline;

    process->kernel_sp = (uint32_t *)stack_top;
    process->process_state = PROCESS_READY;
    if (next_pid >= MAX_PROCESSES)
    {
        // just do nothing for now we have 64 processes allowed
        KERROR("PID wraparound, no more processes can be created!");
        goto fail_kstack;
    }
    process->pid = next_pid++;
    process_table[process->pid] = process;
    process->device_va_next = 0x60000000;
    process->mmap_va_next = 0x20000000;
    process->parent_pid = 0;
    list_init(&process->outstanding_replies);
    list_init(&process->children);
    process->sibling_node.next = NULL;
    process->sibling_node.prev = NULL;
    process->priority = 1;
    process->time_slice = 5;
    process->ticks_remaining = process->time_slice;
    process->node.next = NULL;
    process->node.prev = NULL;
    process->flags = 0; 
    const char *short_name = name;
    for (const char *p = name; *p; p++) {
        if (*p == '/')
            short_name = p + 1;
    }
    strncpy(process->name, short_name, sizeof(process->name) - 1);

    KDEBUG("Created process with name %s PID %u", process->name, process->pid);
    return process;

fail_kstack:
    kstack_free(process->kernel_stack_top);
fail_as:
    // as_destroy() tears down mappings/tables but does not reclaim user backing pages.
    // Reclaim mapped user pages here for all partially loaded process states.
    if (process->as)
        arch_mmu_free_user_pages(process->as->ttbr0_pa);
    as_destroy(process->as);
fail_process:
    handle_vec_destroy(&process->handle_table);
    kfree(process);
    return NULL;
}