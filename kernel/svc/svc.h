#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <arch/regs.h>
#include "kernel/task/task.h"
#include "stdbool.h"
#include "stddef.h"
#include "kernel/mm/vmm/vmm.h"
#include "stdint.h"
#include <zuzu/syscall_nums.h>
#include <zuzu/err.h>


/*
 * zuzu Syscall ABI (ARMv7-A)
 *
 * Syscall numbers encoded in the lower 8 bits of SVC immediate.
 * Arguments in r0-w3, return in r0. See docs/syscall.md for full ABI.
 */

#define CURRENT_SPACE (current_task->owner)

typedef uint8_t Svc;

/**
 * @brief Copies data from a kernel address to a user address, checking for page faults.
 * 
 * @param uaddr The user address to copy from.
 * @param kaddr The kernel address to copy to.
 * @param len The number of bytes to copy.
 * @return true if the copy was successful, false otherwise.
 */
bool CopyToUser(void *restrict uaddr, const void *restrict kaddr, size_t len);

/**
 * @brief Copies data from a kernel address to a user address, checking for page faults.
 * 
 * @param kaddr The kernel address to copy from.
 * @param uaddr The user address to copy to.
 * @param len The number of bytes to copy.
 * @return true if the copy was successful, false otherwise.
 */
bool CopyFromUser(void *restrict kaddr, const void *restrict uaddr, size_t len);

/**
 * @brief Dispatches a service call, handling the appropriate service function.
 * 
 * @param svc_num The service number to dispatch.
 * @param frame The CPU state frame to use for the service call.
 */
void __hot SvcDispatch(Svc svc_num, CpuState *frame);

/**
 * @brief Checks if a user pointer is normal, i.e., within the user address space.
 * 
 * @param addr The user address to check.
 * @param len The number of bytes to check.
 * @return true if the pointer is normal, false otherwise.
 */
static inline bool IsUserPtrNormal(const uintptr_t addr, const size_t len) {
    if (addr + len < addr) return false;
    if (addr >= USER_VA_TOP) return false;
    if (addr + len > USER_VA_TOP) return false;
    return true;
}

extern TaskObject *current_task;

/*
 * Service call functions.
 */

/**
 * @brief Service call function for quitting the process.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcQuit(CpuState *frame);

/**
 * @brief Service call function for yielding the CPU.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcYield(CpuState *frame);

/**
 * @brief Service call function for sleeping.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcSleep(CpuState *frame);

/**
 * @brief Service call function for creating objects.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcCreate(CpuState *frame);

/**
 * @brief Service call function for calling another process.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcCall(CpuState *frame);

/**
 * @brief Service call function for replying to a call.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcReply(CpuState *frame);

/**
 * @brief Service call function for waiting on handles.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcWaitOn(CpuState *frame);

/**
 * @brief Service call function for controlling handles.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcManageHandle(CpuState *frame);

/**
 * @brief Service call function for signaling notifications.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcSignal(CpuState *frame);

/**
 * @brief Service call function for binding events to notifications.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcBind(CpuState *frame);

/**
 * @brief Service call function for controlling memory.
 * 
 * @param frame The CPU state frame to use for the service call.
 */
void SvcManageMemory(CpuState *frame);

#endif /* KERNEL_SYSCALL_H */
