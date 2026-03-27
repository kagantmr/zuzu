#include "sys_dev.h"
#include <mem.h>
#include "kernel/syscall/syscall.h"
#include "kernel/mm/alloc.h"
#include <zuzu/syscall_nums.h>
#include "kernel/proc/process.h"
#include "arch/arm/mmu/mmu.h"
#include <string.h>

#define LOG_FMT(fmt) "(sys_dev) " fmt
#include "core/log.h"

extern process_t *current_process;

void mapdev(exception_frame_t *frame)
{
    
    uint32_t handle_idx = frame->r[0];

    if (handle_idx == 0) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle_idx);
    if (!entry) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (entry->type != HANDLE_DEVICE) {
        frame->r[0] = ERR_BADFORM;
        return;
    }

    device_cap_t *cap = entry->dev;
    if (!cap) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    if (cap->mapped) {
        frame->r[0] = ERR_BUSY;
        return;
    }

    uint32_t size_aligned = align_up(cap->size, 4096);
    uint32_t user_va = current_process->device_va_next;

    if (current_process->mmap_va_next + size_aligned > 0x5FFF0000u) {
        frame->r[0] = ERR_NOMEM;
        return;
    }
    
    if (!vmm_map_range(current_process->as, user_va, cap->phys_base, size_aligned,
                       VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
                       VM_MEM_DEVICE, VM_OWNER_NONE, VM_FLAG_NONE))
    {
        frame->r[0] = ERR_NOMEM;
        return;
    }

    // flush TLB for this VA
    arch_mmu_flush_tlb_va(user_va);
    arch_mmu_barrier();

    current_process->device_va_next += size_aligned;
    cap->mapped = true;
    entry->mapped_va = user_va;

    frame->r[0] = user_va;
}

void querydev(exception_frame_t *frame) {
    uint32_t handle_idx = frame->r[0];
    char *out_buf = (char *)frame->r[1];
    uint32_t buf_len = frame->r[2];

    if (handle_idx == 0 || buf_len == 0) { frame->r[0] = ERR_BADARG; return; }
    if (!validate_user_ptr((uintptr_t)out_buf, buf_len)) { frame->r[0] = ERR_BADARG; return; }

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle_idx);
    if (!entry) { frame->r[0] = ERR_BADARG; return; }
    if (entry->type != HANDLE_DEVICE) { frame->r[0] = ERR_BADFORM; return; }

    device_cap_t *cap = entry->dev;
    if (!cap) { frame->r[0] = ERR_BADARG; return; }

    strncpy(out_buf, cap->compatible, buf_len - 1);
    out_buf[buf_len - 1] = '\0';
    frame->r[0] = cap->irq;  // return irq in r0 as bonus
}