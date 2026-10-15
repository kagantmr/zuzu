#ifndef KERNEL_SVC_SVC_H
#define KERNEL_SVC_SVC_H

#include "kernel/mm/vmm/vmm.h"
#include "kernel/task/task.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"
#include <arch/regs.h>
#include <svc_nums.h>
#include <zuzu/err.h>

#define CURRENT_SPACE (current_task->owner)

bool CopyToUser(void *restrict uaddr, const void *restrict kaddr, size_t len);

bool CopyFromUser(void *restrict kaddr, const void *restrict uaddr, size_t len);

void __hot SvcDispatch(Svc svc_num, CpuState *frame);

static inline bool IsUserPtrNormal(const uintptr_t addr, const size_t len)
{
    if (addr + len < addr)
        return false;
    if (addr >= USER_VA_TOP)
        return false;
    if (addr + len > USER_VA_TOP)
        return false;
    return true;
}

extern TaskObject *current_task;

void SvcQuit(CpuState *frame);

void SvcYield(CpuState *frame);

void SvcSleep(CpuState *frame);

#ifdef DEBUG
void SvcDebugLog(CpuState *frame);
#endif

void SvcCreate(CpuState *frame);

void SvcCall(CpuState *frame);

void SvcReply(CpuState *frame);

void SvcWaitOn(CpuState *frame);

void SvcManageHandle(CpuState *frame);

void SvcSignal(CpuState *frame);

void SvcBind(CpuState *frame);

void SvcManageMemory(CpuState *frame);

void SvcManageTask(CpuState *frame);

void SvcHold(CpuState *frame);

#endif /* KERNEL_SVC_SVC_H */
