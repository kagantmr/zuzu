/**
 * exception.c - ARMv7 exception handling
 */

#include <arch/regs.h>
#include <arch/irq.h>
#include <arch/mmu.h>
#include <arch/fpu.h>
#include "core/log.h"
#include "core/panic.h"
#include "core/ksym.h"
#include "kernel/proc/process.h"
#include "kernel/proc/kstack.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/pmm.h"
#include "kernel/syscall/syscall.h"
#include <string.h>
#include <stdint.h>
#include <snprintf.h>

typedef enum exception_type
{
    EXC_RESET = 0,
    EXC_UNDEF = 1,
    EXC_SVC = 2,
    EXC_PREFETCH_ABORT = 3,
    EXC_DATA_ABORT = 4,
    EXC_RESERVED = 5,
    EXC_IRQ = 6,
    EXC_FIQ = 7
} exception_type;

// Decode FSR status bits (works for both DFSR and IFSR)
static const char *decode_fault_status(uint32_t fsr)
{
    // Status = FS[10] : FS[3:0]
    uint32_t status = (fsr & 0xF) | ((fsr >> 6) & 0x10);

    switch (status)
    {
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

static const char *decode_mode(uint32_t spsr)
{
    switch (spsr & 0x1F)
    {
    case 0x10:
        return "USR";
    case 0x11:
        return "FIQ";
    case 0x12:
        return "IRQ";
    case 0x13:
        return "SVC";
    case 0x16:
        return "MON";
    case 0x17:
        return "ABT";
    case 0x1A:
        return "HYP";
    case 0x1B:
        return "UND";
    case 0x1F:
        return "SYS";
    default:
        return "???";
    }
}

/* addr annotated with its containing kernel symbol, e.g. "0x8012340 (schedule+0x18)". */
static void sym_annotate(char *buf, size_t bufsz, uint32_t addr)
{
    const char *name = ksym_lookup(addr);
    uint32_t base = ksym_lookup_base(addr);
    if (name && base && addr != base)
        snprintf(buf, bufsz, "0x%08X (%s+0x%X)", addr, name, addr - base);
    else if (name)
        snprintf(buf, bufsz, "0x%08X (%s)", addr, name);
    else
        snprintf(buf, bufsz, "0x%08X (<?>)", addr);
}

/* d0-d31 + FPSCR, laid out exactly as arch_fpu_save() (arch/arm/vfp.S) writes them. */
static void dump_vfp(const FpuState *fpu)
{
    const uint8_t *p = (const uint8_t *)fpu;

    for (int i = 0; i < 32; i += 2)
    {
        uint64_t d0, d1;
        memcpy(&d0, p + (size_t)i * 8, 8);
        memcpy(&d1, p + (size_t)(i + 1) * 8, 8);
        kprintf(" d%-2d=%016llX  d%-2d=%016llX\n",
                i, (unsigned long long)d0, i + 1, (unsigned long long)d1);
    }

    uint32_t fpscr;
    memcpy(&fpscr, p + 32 * 8, sizeof(fpscr));
    kprintf(" fpscr=%08X\n", fpscr);
}

static void dump_registers(ExceptionFrame *frame)
{
    char pc_sym[80], lr_sym[80];
    sym_annotate(pc_sym, sizeof(pc_sym), frame->return_pc);
    sym_annotate(lr_sym, sizeof(lr_sym), frame->lr_usr);

    process_t *p = current_thread ? current_thread->owner_process : NULL;

    kprintf("-- register dump --------------------------------------------\n");
    if (p)
        kprintf("  ctx: pid=%u tid=%u '%s'\n", p->pid, current_thread->tid, p->name);
    else if (current_thread)
        kprintf("  ctx: tid=%u (no owner process)\n", current_thread->tid);
    else
        kprintf("  ctx: kernel/boot (no current thread)\n");

    kprintf("  r0=%08X  r1=%08X  r2=%08X  r3=%08X\n",
            frame->r[0], frame->r[1], frame->r[2], frame->r[3]);
    kprintf("  r4=%08X  r5=%08X  r6=%08X  r7=%08X\n",
            frame->r[4], frame->r[5], frame->r[6], frame->r[7]);
    kprintf("  r8=%08X  r9=%08X r10=%08X r11=%08X\n",
            frame->r[8], frame->r[9], frame->r[10], frame->r[11]);
    kprintf(" r12=%08X  sp=%08X\n", frame->r[12], frame->sp_usr);
    kprintf("  lr=%s\n", lr_sym);
    kprintf("  pc=%s\n", pc_sym);
    kprintf("spsr=%08X [%s mode, %s%s%s %c%c%c%c]  frame=%p\n",
            frame->return_cpsr,
            decode_mode(frame->return_cpsr),
            (frame->return_cpsr & (1 << 7)) ? "I" : "i",
            (frame->return_cpsr & (1 << 6)) ? "F" : "f",
            (frame->return_cpsr & (1 << 5)) ? " Thumb" : "",
            (frame->return_cpsr & (1u << 31)) ? 'N' : 'n',
            (frame->return_cpsr & (1u << 30)) ? 'Z' : 'z',
            (frame->return_cpsr & (1u << 29)) ? 'C' : 'c',
            (frame->return_cpsr & (1u << 28)) ? 'V' : 'v',
            (void *)frame);

    uint32_t dfar, dfsr, ifar, ifsr;
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));
    kprintf(" DFAR=%08X  DFSR=%08X  (%s)\n", dfar, dfsr, decode_fault_status(dfsr));
    kprintf(" IFAR=%08X  IFSR=%08X  (%s)\n", ifar, ifsr, decode_fault_status(ifsr));

    /* current_thread == fpu_owner is the only state where CPACR access is
     * enabled for this thread (sched.c keeps the two in lockstep on every
     * switch), so it's the only state where touching the live d0-d31 here
     * won't itself raise an undefined-instruction exception. Otherwise the
     * thread's FPU state (if it has any) is what was last saved into
     * fpu_state on the switch away from it. */
    if (current_thread && current_thread == fpu_owner)
    {
        FpuState live;
        arch_fpu_save(&live);
        kprintf("-- vfp state (live) -------------------------------------------\n");
        dump_vfp(&live);
    }
    else if (current_thread)
    {
        kprintf("-- vfp state (saved, thread is not current fpu owner) --------\n");
        dump_vfp(&current_thread->fpu_state);
    }
}

// Attempts to service a translation-fault dfar via demand paging against
// the process's VM regions. Returns true if handled (caller should return
// immediately without killing/panicking).
static bool try_demand_page(process_t *current_process, uint32_t dfar, uint32_t dfsr)
{
    addrspace_t *as = current_process->as;
    for (uint32_t i = 0; i < as->regions.len; i++)
    {
        vm_region_t *r = vm_region_vec_get(&as->regions, i);
        if (dfar >= r->vaddr_start && dfar < r->vaddr_start + r->size)
        {
            if (r->flags & VM_FLAG_GUARD)
                continue;
            if (r->memtype == VM_MEM_DEVICE)
                continue;
            if (!(dfsr & (1 << 11)) && !(r->prot & PROT_READ))
                continue;
            if ((dfsr & (1 << 11)) && !(r->prot & PROT_WRITE))
                continue;
            uintptr_t page_va = align_down(dfar, PAGE_SIZE);
            return vmm_fault_page(as, r, page_va);
        }
    }
    return false;
}

void exception_dispatch(exception_type exctype, ExceptionFrame *frame)
{
    process_t *current_process = current_thread ? current_thread->owner_process : NULL;

    switch (exctype)
    {
    case EXC_UNDEF:
    {
        /* Undef sets LR = faulting PC + 4 in ARM state but + 2 in Thumb;
         * entry.S subtracts 4 unconditionally, so nudge Thumb faults back. */
        if (frame->return_cpsr & (1 << 5))
            frame->return_pc += 2;

        if (current_thread && current_thread != fpu_owner)
        {
            arch_fpu_trap_enable();
            if (fpu_owner)
                arch_fpu_save(&fpu_owner->fpu_state);
            arch_fpu_restore(&current_thread->fpu_state);
            fpu_owner = current_thread;
            break;
        }

        /**
         * Any other undefined instruction is NOT returnable. Kill process or
         * panic.
         */
        bool from_user = (frame->return_cpsr & 0x1F) == 0x10;

        if (from_user && current_process)
        {
            KERROR("Oops! '%s' (PID %d, TID %d) killed: undefined instruction @ 0x%08X\n", current_process->name, current_process->pid, current_thread->tid, frame->return_pc);
            dump_registers(frame);
            process_kill(current_process, KILLED_TAG | KILL_FAULT_UNDEF);
            schedule();
        }
        else
        {
            panic_fault_ctx = (panic_fault_context_t){
                .valid = 1,
                .fault_type = "Undefined instruction",
                .fault_decoded = "Undefined instruction",
                .frame = frame,
            };
            panic("Kernel-level undefined instruction");
        }
    }
    break;

    case EXC_SVC:
    {
        if ((frame->return_cpsr & 0x1F) != 0x10)
        {
            break;
        }

        uint8_t svc_num;

        if (frame->return_cpsr & (1 << 5))
        {
            // Thumb mode: SVC instruction is 2 bytes, at return_pc - 2
            uint16_t *thumb_instr = (uint16_t *)(frame->return_pc - 2);
            svc_num = (uint8_t)(*thumb_instr & 0xFF);
        }
        else
        {
            // ARM mode: SVC instruction is 4 bytes, at return_pc - 4
            uint32_t *arm_instr = (uint32_t *)(frame->return_pc - 4);
            svc_num = (uint8_t)(*arm_instr & 0xFF);
        }

        SyscallDispatch(svc_num, frame);
    }
    break;

    case EXC_PREFETCH_ABORT:
    {
        /**
         * Prefetch abort is also impossible to return from.
         * This means either pc is corrupted, or we haven't mapped
         * whatever text seciton was trying to be executed.
         * Retrieve IFAR and IFSR and kill process/panic.
         */

        uint32_t ifar, ifsr;
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));
        __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));

        bool from_user = (frame->return_cpsr & 0x1F) == 0x10;

        if (from_user && current_process)
        {
            KERROR("Oops! '%s' (PID %d, TID %d) killed: prefetch abort @ 0x%08X (%s)\n",
                   current_process->name, current_process->pid, current_thread->tid, ifar, decode_fault_status(ifsr));
            process_kill(current_process, KILLED_TAG | KILL_FAULT_PREFETCH);
            dump_registers(frame);
            schedule();
        }
        else
        {
            panic_fault_ctx = (panic_fault_context_t){
                .valid = 1,
                .far = ifar,
                .fsr = ifsr,
                .fault_type = "Prefetch abort",
                .fault_decoded = decode_fault_status(ifsr),
                .frame = frame,
            };
            panic("Kernel-level prefetch abort");
        }
    }
    break;

    case EXC_DATA_ABORT:
    {

        /**
         * Could be anything from a page fault to an alignment issue.
         * Check who's triggered it, and what the reason was.
         */

        uint32_t dfar, dfsr;
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
        __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));

        bool from_user = (frame->return_cpsr & 0x1F) == 0x10;
        bool from_svc  = (frame->return_cpsr & 0x1F) == 0x13;

        uint32_t fault_status = (dfsr & 0xF) | ((dfsr >> 6) & 0x10);
        bool is_translation = (fault_status == 0x05 || fault_status == 0x07);

        /* Kernel stack overflow, guard page hit regardless of source mode */
        if (dfar >= KSTACK_REGION_BASE && dfar < KSTACK_REGION_TOP)
        {
            uint32_t offset_in_slot = (dfar - KSTACK_REGION_BASE) % KSTACK_SLOT_SIZE;
            if (offset_in_slot < 0x1000)
            {
                panic_fault_ctx = (panic_fault_context_t){
                    .valid = 1,
                    .far = dfar,
                    .fsr = dfsr,
                    .fault_type = "Data abort (kernel stack overflow)",
                    .fault_decoded = decode_fault_status(dfsr),
                    .access_type = (dfsr & (1 << 11)) ? "Write" : "Read",
                    .frame = frame,
                };
                panic("Kernel stack overflow");
            }
        }


        if (from_user && current_process && current_process->as)
        {
            if (is_translation && dfar < KERNEL_VA_BASE
                && try_demand_page(current_process, dfar, dfsr))
                return;

            KERROR("Oops! Segmentation fault");
            KDEBUG("Oops! '%s' (PID %d, TID %d) killed: data abort @ 0x%08X (%s %s)\n",
                   current_process->name, current_process->pid, current_thread->tid, dfar,
                   (dfsr & (1 << 11)) ? "write" : "read",
                   decode_fault_status(dfsr));
            dump_registers(frame);
            process_kill(current_process, KILLED_TAG | KILL_FAULT_DATA);
            schedule();
        }
        else if (from_svc && current_process && current_process->as
                 && dfar < KERNEL_VA_BASE)
        {
            if (is_translation && try_demand_page(current_process, dfar, dfsr))
                return;

            KDEBUG("Oops! Bad user pointer in SVC from '%s' (PID %d, TID %d) @ 0x%08X (%s %s)\n",
                   current_process->name, current_process->pid, current_thread->tid, dfar,
                   (dfsr & (1 << 11)) ? "write" : "read",
                   decode_fault_status(dfsr));
            dump_registers(frame);
            process_kill(current_process, KILLED_TAG | KILL_FAULT_DATA);
            schedule();
        }
        else
        {
            /* Kernel VA fault while in SVC mode, or no address space, or
             * any other non-user/non-SVC kernel-mode abort: panic. */
            panic_fault_ctx = (panic_fault_context_t){
                .valid = 1,
                .far = dfar,
                .fsr = dfsr,
                .fault_type = "Data abort",
                .fault_decoded = decode_fault_status(dfsr),
                .access_type = (dfsr & (1 << 11)) ? "Write" : "Read",
                .frame = frame,
            };
            panic("Kernel-level data abort");
        }
    }
    break;

    case EXC_RESERVED:
    {
        // dump_registers(frame);
        panic_fault_ctx = (panic_fault_context_t){
            .valid = 1,
            .fault_type = "Reserved",
            .fault_decoded = "Reserved exception",
            .frame = frame,
        };
        panic("Why are you here? (reserved exception)");
    }
    break;

    case EXC_IRQ:
    {
        arch_irq_dispatch();
    }
    break;

    case EXC_FIQ:
    {
        KERROR("No support for FIQ");
    }
    break;

    default:
    {
        panic_fault_ctx = (panic_fault_context_t){
            .valid = 1,
            .fault_type = "Unknown exception",
            .frame = frame,
        };
        panic("Unknown exception");
    }
    break;
    }
}

/* Called from the exception_exit tripwire in entry.S when the frame about to
 * be RFE'd has return_pc == 0: the frame was corrupted after the C handlers
 * released it. Panic here, in kernel context, with the frame contents. */
_Noreturn void exception_exit_pc0_trap(CpuState *frame)
{
    KERROR("exception_exit: frame at %p has return_pc=0 (cpsr=%p sp_usr=%p lr_usr=%p)",
           frame, (void *)arch_regs_flags(frame), (void *)arch_regs_sp(frame),
           (void *)arch_regs_lr(frame));
    KERROR("  r0=%p r1=%p r2=%p r3=%p r12=%p",
           (void *)*arch_reg(frame, 0), (void *)*arch_reg(frame, 1),
           (void *)*arch_reg(frame, 2), (void *)*arch_reg(frame, 3),
           (void *)*arch_reg(frame, 12));
    panic_fault_ctx = (panic_fault_context_t){
        .valid = 1,
        .fault_type = "RFE to pc=0 (frame corrupted in kernel)",
        .frame = frame,
    };
    panic("exception_exit would resume at pc=0");
}
