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

    // Device mappings are carved from device_va_next; bound-check that cursor.
    if (user_va >= USER_VA_TOP || size_aligned > USER_VA_TOP - user_va) {
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
    char compat_buf[sizeof(((device_cap_t *)0)->compatible) + 1];

    if (handle_idx == 0 || buf_len == 0) { frame->r[0] = ERR_BADARG; return; }

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle_idx);
    if (!entry) { frame->r[0] = ERR_BADARG; return; }
    if (entry->type != HANDLE_DEVICE) { frame->r[0] = ERR_BADFORM; return; }

    device_cap_t *cap = entry->dev;
    if (!cap) { frame->r[0] = ERR_BADARG; return; }

    strncpy(compat_buf, cap->compatible, sizeof(compat_buf) - 1);
    compat_buf[sizeof(compat_buf) - 1] = '\0';

    size_t copy_len = strlen(compat_buf) + 1;
    if (copy_len > buf_len) {
        copy_len = buf_len;
        compat_buf[copy_len - 1] = '\0';
    }

    if (!copy_to_user(out_buf, compat_buf, copy_len)) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    frame->r[0] = cap->irq;  // return irq in r0 as bonus
}
