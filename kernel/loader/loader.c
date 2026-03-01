#include "loader.h"
#include "lib/mem.h"
#include "lib/string.h"
#include "kernel/proc/kstack.h"
#include "kernel/mm/pmm.h"
#include "kernel/vmm/vmm.h"
#include "arch/arm/mmu/mmu.h"
#include "arch/arm/include/cache.h"
#include "kernel/loader/elf.h"
#include "kernel/mm/alloc.h"   

extern uint32_t next_pid;
extern process_t *process_table[MAX_PROCESSES];

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

    // try creating as
    process->as = addrspace_create(ADDRSPACE_USER);
    if (!process->as)
        goto fail_process;

    // try getting a kstack
    uintptr_t stack_top = kstack_alloc();
    if (!stack_top)
        goto fail_as;
    process->kernel_stack_top = stack_top;

    // magic logic removed

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
            // copy segment data to the page we allocated
            uint32_t pages_needed = (ph->p_memsz + PAGE_SIZE - 1) / PAGE_SIZE;
            uintptr_t segment_pa = pmm_alloc_pages(pages_needed);
            if (!segment_pa)
                goto fail_kstack;
            memcpy((void *)PA_TO_VA(segment_pa), (const uint8_t *)elf_data + ph->p_offset, ph->p_filesz);

            if (ph->p_memsz > ph->p_filesz)
                memset((uint8_t *)PA_TO_VA(segment_pa) + ph->p_filesz,
                       0,
                       ph->p_memsz - ph->p_filesz);
            // map the page (or pages) into the process's address space at the specified virtual address
            for (uint32_t page = 0; page < pages_needed; page++)
            {
                uintptr_t va = ph->p_vaddr + page * PAGE_SIZE;
                uintptr_t pa = segment_pa + page * PAGE_SIZE;
                uint32_t prot = 0;
                if (ph->p_flags & PF_R)
                    prot |= VM_PROT_READ;
                if (ph->p_flags & PF_W)
                    prot |= VM_PROT_WRITE;
                if (ph->p_flags & PF_X)
                    prot |= VM_PROT_EXEC;
                if (!kmap_user_page(process->as, pa, va, prot))
                {
                    // unmap any mapped pages (orphans)
                    for (uint32_t j = 0; j < page; j++)
                    {
                        uintptr_t orphan_va = ph->p_vaddr + j * PAGE_SIZE;
                        vmm_unmap_range(process->as, orphan_va, PAGE_SIZE);
                    }
                    for (uint32_t j = page; j < pages_needed; j++)
                    {
                        pmm_free_page(segment_pa + j * PAGE_SIZE);
                    }
                    goto fail_kstack;
                }
            }
            cache_flush_code_range((uintptr_t)PA_TO_VA(segment_pa), ph->p_memsz);  // clear cache so it doesnt have stale data
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

    // write exception frame to the stack
    stack_top -= 16 * sizeof(uint32_t); // 16 words
    uint32_t *exc_frame = (uint32_t *)stack_top;
    *(exc_frame++) = 0;
    for (int i = 0; i < 12; i++)
    {
        *(exc_frame++) = 0; // r1-12 = 0  (indices 0-12)
    }
    *(exc_frame++) = USR_SP;    // lr = SP_usr              (index 13)
    *(exc_frame++) = elf_entry; // PC = entry point    (index 14)
    *(exc_frame++) = 0x10;      // CPSR = 0x10         (index 15)

    // write cpu_context to stack
    stack_top -= sizeof(cpu_context_t);
    cpu_context_t *context = (cpu_context_t *)stack_top;
    memset(context, 0, sizeof(cpu_context_t));
    context->lr = (uint32_t)process_entry_trampoline;

    process->kernel_sp = (uint32_t *)stack_top;
    process->process_state = PROCESS_READY;
    process->pid = next_pid++;
    process_table[process->pid] = process;
    process->device_va_next = 0x60000000;
    process->parent_pid = 0;
    process->priority = 1;
    process->time_slice = 5;
    process->ticks_remaining = process->time_slice;
    process->node.next = NULL;
    process->node.prev = NULL;
    strncpy(process->name, name, sizeof(process->name) - 1);

    // KDEBUG("Created process with magic %X and PID %d", magic, process->pid);
    return process;

fail_kstack:
    kstack_free(process->kernel_stack_top);
fail_as:
    addrspace_destroy(process->as);
fail_process:
    kfree(process);
    return NULL;
}