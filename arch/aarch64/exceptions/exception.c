/**
 * @file
 * @brief AArch64 synchronous exception handling.
 */

#include "core/log.h"
#include "core/panic.h"
#include "kernel/bench.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
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
#include <arch_impl/sysreg.h>
#include <zuzu/user_layout.h>
#include "esr.h"
#include <stdint.h>

#ifdef CONFIG_ZUZU_BENCH
BENCH_STAT(g_bench_lazy_map_fault, "lazy-map translation fault");
BENCH_STAT(g_bench_irq_dispatch, "irq: ArchIrqDispatch");
#endif

const char *DecodeDfsc(uint32_t dfsc)
{
    static const char *const address_size[] = {
        "address size fault, level 0", "address size fault, level 1", "address size fault, level 2",
        "address size fault, level 3"};
    static const char *const translation[] = {
        "translation fault, level 0", "translation fault, level 1", "translation fault, level 2",
        "translation fault, level 3"};
    static const char *const access_flag[] = {
        "access flag fault, level 0", "access flag fault, level 1", "access flag fault, level 2",
        "access flag fault, level 3"};
    static const char *const permission[] = {
        "permission fault, level 0", "permission fault, level 1", "permission fault, level 2",
        "permission fault, level 3"};
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

const char *EcName(EsrClass ec)
{
    switch (ec) {
    case EC_UNKNOWN:
        return "Unknown reason (undefined instruction)";
    case EC_WFX:
        return "Trapped WFI/WFE";
    case EC_FP_SIMD:
        return "FP/SIMD access trap";
    case EC_ILLEGAL_STATE:
        return "Illegal execution state";
    case EC_SVC32:
        return "SVC from AArch32";
    case EC_SVC64:
        return "SVC from AArch64";
    case EC_SYSREG:
        return "Trapped MSR/MRS or system instruction";
    case EC_IABT_LOWER:
        return "Instruction abort from lower EL";
    case EC_IABT_SAME:
        return "Instruction abort from same EL";
    case EC_PC_ALIGN:
        return "PC alignment fault";
    case EC_DABT_LOWER:
        return "Data abort from lower EL";
    case EC_DABT_SAME:
        return "Data abort from same EL";
    case EC_SP_ALIGN:
        return "SP alignment fault";
    case EC_FP_EXC32:
        return "FP exception from AArch32";
    case EC_FP_EXC64:
        return "FP exception from AArch64";
    case EC_SERROR:
        return "SError";
    case EC_BREAKPOINT_LOWER:
        return "Breakpoint from lower EL";
    case EC_BREAKPOINT_SAME:
        return "Breakpoint from same EL";
    case EC_SOFTSTEP_LOWER:
        return "Software step from lower EL";
    case EC_SOFTSTEP_SAME:
        return "Software step from same EL";
    case EC_WATCHPOINT_LOWER:
        return "Watchpoint from lower EL";
    case EC_WATCHPOINT_SAME:
        return "Watchpoint from same EL";
    case EC_BKPT32:
        return "BKPT from AArch32";
    case EC_BRK64:
        return "BRK instruction";
    default:
        return "Unknown exception class";
    }
}

/**
 * @brief Handles a synchronous exception from the kernel, mostly by reporting the fault 
 * and then panicking.
 * 
 * @param[in, out] frame The CPU frame at the time the exception was raised.
 */
void KernelSyncException(CpuState *frame);

void KernelSyncException(CpuState *frame)
{
    Register esr = (Register)READ_SYSREG(esr_el1);
    Register far = (Register)READ_SYSREG(far_el1);

    EsrClass ec = EsrEc((uint64_t)esr);
    uint32_t iss = EsrIss((uint64_t)esr);

    panic_fault_ctx = (PanicFaultContext){
        .valid = 1,
        .fsr = esr,
        .frame = frame,
    };

    // fill in the rest depending on the class
    switch (ec) {
    case EC_DABT_SAME: {
        VirtAddr fa = (VirtAddr)far;
        bool write = EsrIsWrite(iss);

        panic_fault_ctx.far = far;
        panic_fault_ctx.far_valid = 1;
        panic_fault_ctx.fault_decoded = DecodeDfsc(iss & 0x3F);
        panic_fault_ctx.access_type = write ? "write" : "read";

        if (fa >= KSTACK_REGION_BASE && fa < KSTACK_REGION_TOP &&
            (fa - KSTACK_REGION_BASE) % KSTACK_SLOT_SIZE < KSTACK_GUARD_SIZE) {
            panic_fault_ctx.fault_type = "Data abort (kernel stack overflow)";
            panic("Kernel stack overflow");
        }

        if (fa < USER_VA_TOP && current_task && current_task->owner) {
            SpaceObject *space = current_task->owner;
            if (EsrIsTranslationFault(iss) && VmmDemandPage(space->as, fa, write))
                return;

            KERROR("Oops! Kernel fault on a user address in a svc from '%s' (SPID %d, TID %d) "
                   "pc=" REG_FMT " @ " REG_FMT " (%s %s)",
                   space->name, space->spid, current_task->tid, REG_ARG(frame->return_pc),
                   REG_ARG(far), write ? "write" : "read", DecodeDfsc(iss & 0x3F));
            panic_fault_ctx.valid = 0;
            TaskFault(current_task, KILLED_FAULT_DATA);
            Schedule();
            return;
        }

        panic_fault_ctx.fault_type = "Data abort";
        panic("Kernel data abort");
    }

    case EC_IABT_SAME:
        panic_fault_ctx.far = far;
        panic_fault_ctx.far_valid = 1;
        panic_fault_ctx.fault_type = "Instruction abort";
        panic_fault_ctx.fault_decoded = DecodeDfsc(iss & 0x3F);
        panic_fault_ctx.access_type = "execute";
        panic("Kernel instruction abort at " REG_FMT, REG_ARG(far));
    case EC_UNKNOWN:
        panic_fault_ctx.fault_type = EcName(ec);
        panic("Kernel-level undefined instruction");
    case EC_BRK64:
    case EC_PC_ALIGN:
    case EC_SP_ALIGN:
        panic_fault_ctx.fault_type = EcName(ec);
        panic("Synchronous exception (%s)", panic_fault_ctx.fault_type);

    default:
        panic_fault_ctx.fault_type = EcName(ec);
        panic("unexpected sync exception, EC=0x%x", (unsigned)ec);
    }
}

/**
 * @brief Called from the vectors zuzu never expects; panics.
 *
 * Covers EL1 running on SP_EL0, every FIQ, and the EL0 slots until user tasks exist.
 *
 * @param[in] frame  Registers at the exception.
 * @param[in] offset Offset of the vector slot in the table.
 */
_Noreturn void BadVector(CpuState *frame, uint64_t offset);

_Noreturn void BadVector(CpuState *frame, uint64_t offset)
{
    static const char *const origin[] = {"EL1 on SP_EL0", "EL1 on SP_EL1", "EL0 AArch64",
                                         "EL0 AArch32"};
    static const char *const kind[] = {"sync", "IRQ", "FIQ", "SError"};

    Register esr = (Register)READ_SYSREG(esr_el1);
    Register far = (Register)READ_SYSREG(far_el1);
    const char *from = origin[(offset >> 9) & 0x3];
    const char *type = kind[(offset >> 7) & 0x3];

    panic_fault_ctx = (PanicFaultContext){
        .valid = 1,
        .fsr = esr,
        .far = far,
        .fault_type = "Unexpected exception vector",
        .fault_decoded = EcName(EsrEc((uint64_t)esr)),
        .frame = frame,
    };
    panic("Unexpected %s from %s (vector 0x%03x)", type, from, (unsigned)offset);
}
