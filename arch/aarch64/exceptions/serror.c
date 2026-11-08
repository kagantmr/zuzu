/**
 * @file serror.c
 * 
 * @brief AArch64 asynchronous error handler. Panics immediately.
 */

#include <arch/regs.h>
#include <arch_impl/sysreg.h>
#include "core/panic.h"

/**
 * @brief Handles an AArch64 System Error. Never returns.
 * 
 * @param[in] frame The CPU frame at the time the SError was raised.
 */
_Noreturn void SErrorException(CpuState *frame);

_Noreturn void SErrorException(CpuState *frame)
{
    uint64_t esr = READ_SYSREG(esr_el1);
    panic_fault_ctx = (PanicFaultContext){
        .valid = 1, .fsr = (Register)esr, .far = 0, .fault_type = "SError",
        .fault_decoded = "asynchronous abort (bus/memory error)", .frame = frame,
    };
    panic("SError, ESR=" REG_FMT, REG_ARG(esr));
}