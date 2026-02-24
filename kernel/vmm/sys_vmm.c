#include "sys_vmm.h"
#include "kernel/syscall/syscall.h"
#include "kernel/sched/sched.h"
#include "arch/arm/include/irq.h"
#include "kernel/layout.h"
#include "lib/mem.h"

#define LOG_FMT(fmt) "(syscall_vmm) " fmt
#include "core/log.h"

extern process_t *current_process;
extern phys_region_t phys_region;

static bool is_device_phys(uintptr_t pa, size_t size)
{
    // Reject anything that falls inside RAM
    if (pa >= phys_region.start && pa < phys_region.end)
        return false;
    if (pa + size > phys_region.start && pa + size <= phys_region.end)
        return false;
    return true;
}

void memmap(exception_frame_t *frame)
{
    (void)frame;
}
void memunmap(exception_frame_t *frame)
{
    (void)frame;
}
void memshare(exception_frame_t *frame)
{
    (void)frame;
}
void attach(exception_frame_t *frame)
{
    (void)frame;
}

void mapdev(exception_frame_t *frame)
{
    uintptr_t addr_pa = (uintptr_t)frame->r[0];
    uint32_t size = (uint32_t)frame->r[1];
    if (!is_device_phys(addr_pa, size))
    {
        frame->r[0] = ERR_NOPERM;
        return;
    }
    uint32_t user_va = current_process->device_va_next;
    uint32_t size_aligned = align_up(size, 4096);

    if (!vmm_map_range(current_process->as, user_va, addr_pa, size_aligned, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER, VM_MEM_DEVICE, VM_OWNER_NONE, VM_FLAG_NONE))
    {
        frame->r[0] = ERR_NOMEM;
        return;
    }
    current_process->device_va_next += size_aligned;
    frame->r[0] = user_va;
    return;
}