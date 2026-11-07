/**
 * @file
 * @brief AArch64 synchronous exception handling.
 */

#include "core/log.h"
#include "core/panic.h"
#include "kernel/bench.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/sched/sched.h"
#include "kernel/space/space.h"
#include "kernel/svc/svc.h"
#include "kernel/task/kstack.h"
#include "kernel/task/task.h"
#include "util/log.h"
#include <arch/fpu.h>
#include <arch/irq.h>
#include <arch/mmu.h>
#include <arch/regs.h>
#include <stdint.h>

#ifdef CONFIG_ZUZU_BENCH
BENCH_STAT(g_bench_lazy_map_fault, "lazy-map translation fault");
BENCH_STAT(g_bench_irq_dispatch, "irq: ArchIrqDispatch");
#endif

/**
 * @brief Describes the fault status code of a data or instruction abort.
 *
 * @param[in] dfsc DFSC or IFSC field, ESR_EL1.ISS[5:0].
 * @return A static description, with the translation level where the code has one.
 */
static const char *DecodeDfsc(uint32_t dfsc)
{
    static const char *const address_size[] = {
        "address size fault, level 0", "address size fault, level 1",
        "address size fault, level 2", "address size fault, level 3"};
    static const char *const translation[] = {
        "translation fault, level 0", "translation fault, level 1",
        "translation fault, level 2", "translation fault, level 3"};
    static const char *const access_flag[] = {
        "access flag fault, level 0", "access flag fault, level 1",
        "access flag fault, level 2", "access flag fault, level 3"};
    static const char *const permission[] = {
        "permission fault, level 0", "permission fault, level 1",
        "permission fault, level 2", "permission fault, level 3"};
    static const char *const walk_external[] = {
        "external abort on table walk, level 0", "external abort on table walk, level 1",
        "external abort on table walk, level 2", "external abort on table walk, level 3"};
    static const char *const walk_parity[] = {
        "parity/ECC error on table walk, level 0", "parity/ECC error on table walk, level 1",
        "parity/ECC error on table walk, level 2", "parity/ECC error on table walk, level 3"};

    const uint32_t level = dfsc & 0x3U;

    switch (dfsc >> 2) {
    case 0x0:
        return address_size[level];
    case 0x1:
        return translation[level];
    case 0x2:
        return access_flag[level];
    case 0x3:
        return permission[level];
    case 0x5:
        return walk_external[level];
    case 0x7:
        return walk_parity[level];
    default:
        break;
    }

    switch (dfsc) {
    case 0x10:
        return "synchronous external abort";
    case 0x11:
        return "synchronous tag check fault";
    case 0x18:
        return "synchronous parity/ECC error";
    case 0x21:
        return "alignment fault";
    case 0x30:
        return "TLB conflict abort";
    case 0x31:
        return "unsupported atomic hardware update";
    case 0x34:
        return "lockdown abort";
    case 0x35:
        return "unsupported exclusive or atomic access";
    default:
        return "unknown fault status";
    }
}

/**
 * @brief Names an exception class.
 *
 * @param[in] ec Exception class, ESR_EL1[31:26].
 * @return A static name.
 */
static const char *EcName(uint32_t ec)
{
    switch (ec) {
    case 0x00:
        return "Unknown reason (undefined instruction)";
    case 0x01:
        return "Trapped WFI/WFE";
    case 0x07:
        return "FP/SIMD access trap";
    case 0x0E:
        return "Illegal execution state";
    case 0x11:
        return "SVC from AArch32";
    case 0x15:
        return "SVC from AArch64";
    case 0x18:
        return "Trapped MSR/MRS or system instruction";
    case 0x20:
        return "Instruction abort from lower EL";
    case 0x21:
        return "Instruction abort from same EL";
    case 0x22:
        return "PC alignment fault";
    case 0x24:
        return "Data abort from lower EL";
    case 0x25:
        return "Data abort from same EL";
    case 0x26:
        return "SP alignment fault";
    case 0x28:
        return "FP exception from AArch32";
    case 0x2C:
        return "FP exception from AArch64";
    case 0x2F:
        return "SError";
    case 0x30:
        return "Breakpoint from lower EL";
    case 0x31:
        return "Breakpoint from same EL";
    case 0x32:
        return "Software step from lower EL";
    case 0x33:
        return "Software step from same EL";
    case 0x34:
        return "Watchpoint from lower EL";
    case 0x35:
        return "Watchpoint from same EL";
    case 0x38:
        return "BKPT from AArch32";
    case 0x3C:
        return "BRK instruction";
    default:
        return "Unknown exception class";
    }
}

void KernelSyncException(CpuState *frame);

void KernelSyncException(CpuState *frame)
{
    Register esr = READ_SYSREG(esr_el1);
    Register far = READ_SYSREG(far_el1);

    uint32_t ec = (uint32_t)(esr >> 26);        // what kind of exception
    uint32_t iss = (uint32_t)(esr & 0x1FFFFFF); // details, meaning depends on ec

    panic_fault_ctx = (PanicFaultContext){
        .valid = 1,
        .fsr = esr,
        .frame = frame,
    };

    // fill in the rest depending on the class
    switch (ec) {
    case 0x25: // data abort, same EL
        panic_fault_ctx.far = far;
        panic_fault_ctx.fault_type = "Data abort";
        panic_fault_ctx.fault_decoded = DecodeDfsc(iss & 0x3F);
        panic_fault_ctx.access_type = (iss & (1U << 6)) ? "write" : "read";
        panic("Kernel data abort");

    case 0x21: // instruction abort, same EL (jumped somewhere bad)

    case 0x00: // undefined instruction
    case 0x3C: // brk / __builtin_trap()
    case 0x22: // pc misaligned
    case 0x26: // sp misaligned
        panic_fault_ctx.fault_type = EcName(ec);
        panic("Synchronous exception (%s)", panic_fault_ctx.fault_type);

    default: // anything else, kernel svc (0x15) included
        panic_fault_ctx.fault_type = EcName(ec);
        panic("unexpected sync exception, EC=0x%x", ec);
    }
}
