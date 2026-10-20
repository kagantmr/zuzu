/**
 * @file
 * @brief ARMv7-A exception handling: undefined instructions, aborts, IRQs.
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

/** @brief Exception kinds, numbered by their vector slot. */
typedef enum {
    EXCEPTION_UNDEF = 1,          /**< Undefined instruction, including lazy VFP traps. */
    EXCEPTION_PREFETCH_ABORT = 3, /**< Instruction fetch fault. */
    EXCEPTION_DATA_ABORT = 4,     /**< Data access fault. */
    EXCEPTION_RESERVED = 5,       /**< Unused vector. */
    EXCEPTION_IRQ = 6,            /**< Interrupt. */
    EXCEPTION_FIQ = 7             /**< Fast interrupt; not supported. */
} ExceptionType;

/**
 * @brief Describes the status field of a DFSR or IFSR.
 *
 * @param[in] fsr Fault status register value.
 * @return A short description, or "Unknown fault".
 */
static const char *DecodeFsr(uint32_t fsr)
{
    uint32_t status = (fsr & 0xF) | ((fsr >> 6) & 0x10);

    switch (status) {
    case 0x01:
        return "Alignment fault";
    case 0x02:
        return "Debug event";
    case 0x03:
        return "Access flag fault (section)";
    case 0x04:
        return "Instruction cache maintenance fault";
    case 0x05:
        return "Translation fault (section)";
    case 0x06:
        return "Access flag fault (page)";
    case 0x07:
        return "Translation fault (page)";
    case 0x08:
        return "Synchronous external abort";
    case 0x09:
        return "Domain fault (section)";
    case 0x0B:
        return "Domain fault (page)";
    case 0x0C:
        return "External abort on table walk (L1)";
    case 0x0D:
        return "Permission fault (section)";
    case 0x0E:
        return "External abort on table walk (L2)";
    case 0x0F:
        return "Permission fault (page)";
    case 0x10:
        return "TLB conflict abort";
    case 0x16:
        return "Asynchronous external abort";
    case 0x19:
        return "Parity error on memory access";
    default:
        return "Unknown fault";
    }
}

/**
 * @brief Maps the page behind a translation fault if it falls in a region that is filled on demand.
 *
 * Guard and device regions, and accesses the region's protection forbids, are not served.
 *
 * @param[in] space Faulting space.
 * @param[in] dfar  Faulting address.
 * @param[in] dfsr  Fault status; bit 11 set means a write.
 * @return true if the page is now mapped and the access can be retried.
 */
static bool __hot ServiceDemandPage(SpaceObject *space, uint32_t dfar, uint32_t dfsr)
{
    AddressSpace *as = space->as;
    for (uint32_t i = 0; i < as->regions.len; i++) {
        VirtMemRegion *r = vm_region_vec_get(&as->regions, i);
        if (!r)
            continue;
        if (dfar >= r->vaddr_start && dfar < r->vaddr_start + r->size) {
            if (unlikely(r->flags & VM_FLAG_GUARD))
                continue;
            if (unlikely(r->memtype == VM_MEM_DEVICE))
                continue;
            if (unlikely(!(dfsr & (1 << 11)) && !(r->prot & PROT_READ)))
                continue;
            if (unlikely((dfsr & (1 << 11)) && !(r->prot & PROT_WRITE)))
                continue;
            uintptr_t page_va = align_down(dfar, PAGE_SIZE);
            return VmmPageFaultHandle(as, r, page_va);
        }
    }
    return false;
}

/**
 * @brief Handles every exception except SVC, called from the vectors.
 *
 * User faults kill the task; kernel faults panic, except demand-paging faults
 * on user addresses during a syscall.
 *
 * @param[in]     exctype Which exception.
 * @param[in,out] frame   Saved registers.
 */
void __hot ExceptionDispatch(ExceptionType exctype, CpuState *frame);

void __hot ExceptionDispatch(ExceptionType exctype, CpuState *frame)
{
    SpaceObject *current_space = current_task ? current_task->owner : NULL;

    switch (exctype) {
    case EXCEPTION_UNDEF: {
        
        if (frame->return_cpsr & (1 << 5))
            frame->return_pc += 2;

        if (current_task && current_task != fpu_owner) {
            ArchFpuEnableAccess();
            fpu_access_enabled = true;
            if (fpu_owner)
                ArchFpuSaveState(&fpu_owner->fpu_state);
            ArchFpuRestoreState(&current_task->fpu_state);
            fpu_owner = current_task;
            break;
        }

        bool from_user = (frame->return_cpsr & 0x1F) == 0x10;

        if (from_user && current_space) {
            KERROR("Oops! '%s' (SPID %d, TID %d) faulted: undefined instruction @ 0x%08X\n",
                   current_space->name, current_space->spid, current_task->tid, frame->return_pc);
            TaskFault(current_task, KILLED_FAULT_UNDEF);
            Schedule();
        } else {
            panic_fault_ctx = (PanicFaultContext){
                .valid = 1,
                .fault_type = "Undefined instruction",
                .fault_decoded = "Undefined instruction",
                .frame = frame,
            };
            panic("Kernel-level undefined instruction");
        }
    } break;

    case EXCEPTION_PREFETCH_ABORT: {
        
        uint32_t ifar, ifsr;
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));
        __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));

        bool from_user = (frame->return_cpsr & 0x1F) == 0x10;

        if (from_user && current_space) {
            KERROR("Oops! '%s' (SPID %d, TID %d) killed: prefetch abort @ 0x%08X (%s)\n",
                   current_space->name, current_space->spid, current_task->tid, ifar,
                   DecodeFsr(ifsr));
            TaskFault(current_task, KILLED_FAULT_PREFETCH);
            Schedule();
        } else {
            panic_fault_ctx = (PanicFaultContext){
                .valid = 1,
                .far = ifar,
                .fsr = ifsr,
                .fault_type = "Prefetch abort",
                .fault_decoded = DecodeFsr(ifsr),
                .frame = frame,
            };
            panic("Kernel-level prefetch abort");
        }
    } break;

    case EXCEPTION_DATA_ABORT: {
#ifdef CONFIG_ZUZU_BENCH
        uint32_t bench_start = BENCH_BEGIN();
#endif

        uint32_t dfar, dfsr;
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
        __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));

        bool from_user = (frame->return_cpsr & 0x1F) == 0x10;
        bool from_svc = (frame->return_cpsr & 0x1F) == 0x13;

        uint32_t fault_status = (dfsr & 0xF) | ((dfsr >> 6) & 0x10);
        bool is_translation = (fault_status == 0x05 || fault_status == 0x07);

        if (dfar >= KSTACK_REGION_BASE && dfar < KSTACK_REGION_TOP) {
            uint32_t offset_in_slot = (dfar - KSTACK_REGION_BASE) % KSTACK_SLOT_SIZE;
            if (offset_in_slot < 0x1000) {
                panic_fault_ctx = (PanicFaultContext){
                    .valid = 1,
                    .far = dfar,
                    .fsr = dfsr,
                    .fault_type = "Data abort (kernel stack overflow)",
                    .fault_decoded = DecodeFsr(dfsr),
                    .access_type = (dfsr & (1 << 11)) ? "Write" : "Read",
                    .frame = frame,
                };
                panic("Kernel stack overflow");
            }
        }

        if (likely(from_user && current_space && current_space->as)) {
            
            if (likely(is_translation && dfar < KERNEL_VA_BASE) &&
                ServiceDemandPage(current_space, dfar, dfsr)) {
#ifdef CONFIG_ZUZU_BENCH
                BENCH_END(g_bench_lazy_map_fault, bench_start);
#endif
                return;
            }

            KERROR("Oops! Segmentation fault");
            KDEBUG("Oops! '%s' (SPID %d, TID %d) killed: data abort @ 0x%08X (%s %s)\n",
                   current_space->name, current_space->spid, current_task->tid, dfar,
                   (dfsr & (1 << 11)) ? "write" : "read", DecodeFsr(dfsr));
            TaskFault(current_task, KILLED_FAULT_DATA);
            Schedule();
        } else if (from_svc && current_space && current_space->as && dfar < KERNEL_VA_BASE) {
            if (is_translation && ServiceDemandPage(current_space, dfar, dfsr)) {
#ifdef CONFIG_ZUZU_BENCH
                BENCH_END(g_bench_lazy_map_fault, bench_start);
#endif
                return;
            }

            KERROR("Oops! Kernel fault in SVC from '%s' (SPID %d, TID %d, state %d) pc=0x%08X @ "
                   "0x%08X (%s %s)\n",
                   current_space->name, current_space->spid, current_task->tid,
                   (int)current_task->state, (unsigned)frame->return_pc, dfar,
                   (dfsr & (1 << 11)) ? "write" : "read", DecodeFsr(dfsr));
            TaskFault(current_task, KILLED_FAULT_DATA);
            Schedule();
        } else {
            
            panic_fault_ctx = (PanicFaultContext){
                .valid = 1,
                .far = dfar,
                .fsr = dfsr,
                .fault_type = "Data abort",
                .fault_decoded = DecodeFsr(dfsr),
                .access_type = (dfsr & (1 << 11)) ? "Write" : "Read",
                .frame = frame,
            };
            panic("Kernel-level data abort");
        }
    } break;
    case EXCEPTION_IRQ: {
#ifdef CONFIG_ZUZU_BENCH
        uint32_t bench_start = BENCH_BEGIN();
#endif
        ArchIrqDispatch();
#ifdef CONFIG_ZUZU_BENCH
        BENCH_END(g_bench_irq_dispatch, bench_start);
#endif
    } break;

    case EXCEPTION_FIQ: {
        KERROR("No support for FIQ");
    } break;

    case EXCEPTION_RESERVED:
    default: {
        panic_fault_ctx = (PanicFaultContext){
            .valid = 1,
            .fault_type = "Unknown exception",
            .frame = frame,
        };
        panic("Unknown exception");
    } break;
    }
}
