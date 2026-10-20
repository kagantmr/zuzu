/**
 * @file
 * @brief ARMv7-A SVC entry into the syscall dispatcher.
 */

#include "kernel/svc/svc.h"
#include <arch/regs.h>
#include <compiler.h>

/**
 * @brief Called from the SVC vector; dispatches a syscall made from user mode.
 *
 * SVCs from any other mode are ignored.
 *
 * @param[in,out] frame Caller's registers; the syscall number is in r12.
 */
void __hot SvcTrap(CpuState *frame);

void __hot SvcTrap(CpuState *frame)
{
    if (unlikely((frame->return_cpsr & 0x1F) != 0x10))
        return;
    SvcDispatch(ArchFrameSvcNum(frame), frame);
}
