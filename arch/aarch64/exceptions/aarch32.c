/**
 * @file
 * @brief Entry from EL0 in AArch32 state, which zuzu never runs
 */

#include "core/panic.h"
#include <arch/regs.h>
#include <arch_impl/sysreg.h>
#include <stdint.h>

/**
 * @brief Called from the AArch32 EL0 vectors; panics.
 *
 * EL0 only enters AArch32 if the kernel returned to it with SPSR_EL1.M[4] set, which no zuzu
 * path does, so reaching this means a frame was built or corrupted wrongly.
 *
 * @param[in] frame Registers of the AArch32 context.
 */
_Noreturn void AArch32Entry(CpuState *frame);

_Noreturn void AArch32Entry(CpuState *frame)
{
    uint64_t esr = READ_SYSREG(esr_el1);
    panic_fault_ctx = (PanicFaultContext){
        .valid = 1,
        .fsr = (Register)esr,
        .far = 0,
        .fault_type = "AArch32 EL0 entry",
        .fault_decoded = "returned to EL0 in AArch32 state",
        .frame = frame,
    };
    panic("How the hell did I get into AArch32????? ESR=" REG_FMT, REG_ARG(esr));
}
