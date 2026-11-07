/**
 * @file
 * @brief AArch64 SVC entry into the syscall dispatcher.
 */

#include "kernel/svc/svc.h"
#include <arch/regs.h>
#include <arch_impl/sysreg.h>
#include <compiler.h>

/**
 * @brief Dispatches a syscall made from EL0; called for ESR_EL1.EC 0x15.
 *
 * The syscall number is the svc immediate, ESR_EL1.ISS[15:0]. ELR_EL1 already points past the
 * svc, so returning resumes after it.
 *
 * @param[in,out] frame Caller's registers; arguments in x0-x3, results written back there.
 * @param[in]     esr   ESR_EL1 of the exception.
 */
void __hot SvcTrap(CpuState *frame, uint64_t esr);

void __hot SvcTrap(CpuState *frame, uint64_t esr)
{
    if (unlikely((frame->return_pstate & 0xF) != 0))
        return;
    SvcDispatch((uint32_t)(esr & 0xFFFF), frame);
}
