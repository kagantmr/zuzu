#include "arch/arm/include/context.h"
#include "arch/arm/include/irq.h"
#include "core/log.h"
#include "core/panic.h"
#include "kernel/proc/process.h"
#include "kernel/proc/kstack.h"
#include "kernel/sched/sched.h"
#include "kernel/syscall/syscall.h"
#include <stdint.h>

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
    case 0x17:
        return "ABT";
    case 0x1B:
        return "UND";
    case 0x1F:
        return "SYS";
    default:
        return "???";
    }
}

static void dump_registers(exception_frame_t *frame)
{
    kprintf("\033[31m"); // Red
    kprintf("  r0=%08X  r1=%08X  r2=%08X  r3=%08X\n",
            frame->r[0], frame->r[1], frame->r[2], frame->r[3]);
    kprintf("  r4=%08X  r5=%08X  r6=%08X  r7=%08X\n",
            frame->r[4], frame->r[5], frame->r[6], frame->r[7]);
    kprintf("  r8=%08X  r9=%08X r10=%08X r11=%08X\n",
            frame->r[8], frame->r[9], frame->r[10], frame->r[11]);
    kprintf(" r12=%08X  sp=????????  lr=%08X  pc=%08X\n",
            frame->r[12], frame->lr, frame->return_pc);
    kprintf("spsr=%08X [%s mode, %s%s%s]\n",
            frame->return_cpsr,
            decode_mode(frame->return_cpsr),
            (frame->return_cpsr & (1 << 7)) ? "I" : "i",
            (frame->return_cpsr & (1 << 6)) ? "F" : "f",
            (frame->return_cpsr & (1 << 5)) ? " Thumb" : "");
    kprintf("\033[0m"); // Reset
}

extern process_t *current_process;

void exception_dispatch(exception_type exctype, exception_frame_t *frame)
{
    switch (exctype)
    {
    case EXC_UNDEF:
    {
        KERROR("=== UNDEFINED INSTRUCTION ===");

        if (current_process)
        {
            KERROR("Process with ID %d failed at 0x%08X", current_process->pid, frame->return_pc);
            process_t *dying = current_process;
            current_process = NULL;
            dying->process_state = PROCESS_ZOMBIE;
            sched_defer_destroy(dying);
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

        syscall_dispatch(svc_num, frame);
    }
    break;

    case EXC_PREFETCH_ABORT:
    {
        uint32_t ifar, ifsr;
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));
        __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));

        KERROR("=== PREFETCH ABORT ===");
        KERROR("IFAR: 0x%08X  IFSR: 0x%08X", ifar, ifsr);
        KERROR("Fault: %s", decode_fault_status(ifsr));
        KERROR("PC: 0x%08X", frame->return_pc);

        bool from_user = (frame->return_cpsr & 0x1F) == 0x10;

        if (from_user && current_process)
        {
            KERROR("Process with ID %d failed at 0x%08X", current_process->pid, frame->return_pc);
            process_t *dying = current_process;
            current_process = NULL;
            dying->process_state = PROCESS_ZOMBIE;
            dump_registers(frame);
            sched_defer_destroy(dying);
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
        uint32_t dfar, dfsr;
        __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
        __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));

        KERROR("=== DATA ABORT ===");
        KERROR("DFAR: 0x%08X  DFSR: 0x%08X", dfar, dfsr);
        KERROR("Fault: %s", decode_fault_status(dfsr));
        KERROR("Access: %s, %s",
               (dfsr & (1 << 11)) ? "Write" : "Read",
               (dfsr & (1 << 12)) ? "External" : "Internal");
        KERROR("Domain: %u", (dfsr >> 4) & 0xF);

        bool from_user = (frame->return_cpsr & 0x1F) == 0x10;


        if (from_user && current_process)
        {
            KERROR("Process with ID %d failed at 0x%08X", current_process->pid, frame->return_pc);

            if (dfar >= KSTACK_REGION_BASE && dfar < KSTACK_REGION_BASE + 64 * 0x2000)
            {
                // Check if it hit a guard page (low 4KB of each 8KB slot)
                uint32_t offset_in_slot = (dfar - KSTACK_REGION_BASE) % 0x2000;
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
            process_t *dying = current_process;
            current_process = NULL;
            dying->process_state = PROCESS_ZOMBIE;
            // dump_registers(frame);
            sched_defer_destroy(dying);
            schedule();
        }
        else
        {
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
        KWARN("=== RESERVED EXCEPTION (Unused) ===");
        // dump_registers(frame);
        // panic();
    }
    break;

    case EXC_IRQ:
    {
        irq_dispatch();
    }
    break;

    case EXC_FIQ:
    {
        panic_fault_ctx = (panic_fault_context_t){
            .valid = 1,
            .fault_type = "FIQ",
            .fault_decoded = "FIQ not supported",
            .frame = frame,
        };
        panic("No support for FIQ");
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
