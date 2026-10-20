/**
 * @file svc.h
 * @brief Implementation of svc utilities.
 */

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

/**
 * @brief Macro for the current space (current task's owner space)
 */
#define CURRENT_SPACE (current_task->owner)

/**
 * @brief Copy out a kernel buffer to a user buffer, checking for faults.
 *
 * @param[out] uaddr The user address.
 * @param[in] kaddr The kernel address.
 * @param[in] len The length of the data.
 *
 * @return bool
 * @retval true if the copy succeeded
 */
bool CopyToUser(void *restrict uaddr, const void *restrict kaddr, size_t len);

/**
 * @brief Copy in a user buffer to a kernel buffer, checking for faults.
 *
 * @param[in] kaddr The kernel address.
 * @param[out] uaddr The user address.
 * @param[in] len The length of the data.
 *
 * @return bool
 * @retval true if the copy succeeded
 */
bool CopyFromUser(void *restrict kaddr, const void *restrict uaddr, size_t len);

/**
 * @brief Dispatch the incoming svc.
 * 
 * @param[in] svc_num The svc number to dispatch.
 * @param[in,out] frame The CPU frame to act on.
 *
 */
void __hot SvcDispatch(Svc svc_num, CpuState *frame);

/**
 * @brief Inline general checker for userspace pointers.
 * 
 * @param[in] addr The address to check.
 * @param[in] len The size of the buffer.
 *
 * @return true if the range does not wrap and lies entirely below the top of user space.
 */
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

/**
 * @brief External definition of the current task pointer.
 */
extern TaskObject *current_task;

/**
 * @brief Entry point for the Quit syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcQuit(CpuState *frame);

/**
 * @brief Entry point for the Yield syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcYield(CpuState *frame);

/**
 * @brief Entry point for the Sleep syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcSleep(CpuState *frame);

#ifdef DEBUG
/**
 * @brief Entry point for the Log syscall (debug builds only).
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcDebugLog(CpuState *frame);
#endif

/**
 * @brief Entry point for the Create syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcCreate(CpuState *frame);

/**
 * @brief Entry point for the Call syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcCall(CpuState *frame);

/**
 * @brief Entry point for the Reply and ReplyRecv syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcReply(CpuState *frame);

/**
 * @brief Entry point for the WaitOn syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcWaitOn(CpuState *frame);

/**
 * @brief Entry point for the ManageHandle syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcManageHandle(CpuState *frame);

/**
 * @brief Entry point for the Signal syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcSignal(CpuState *frame);

/**
 * @brief Entry point for the Bind syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcBind(CpuState *frame);

/**
 * @brief Entry point for the ManageMemory syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcManageMemory(CpuState *frame);

/**
 * @brief Entry point for the ManageTask syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcManageTask(CpuState *frame);

/**
 * @brief Entry point for the Hold syscall.
 *
 * @param[in,out] frame The caller's trap frame: arguments in, results out.
 */
void SvcHold(CpuState *frame);

#endif /* KERNEL_SVC_SVC_H */
