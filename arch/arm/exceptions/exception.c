
#include "context.h"
#include "core/log.h"
#include <stdint.h>

typedef enum exception_type {
    EXC_RESET = 0,
    EXC_UNDEF = 1,
    EXC_SVC   = 2,
    EXC_PREFETCH_ABORT = 3,
    EXC_DATA_ABORT = 4,
    EXC_RESERVED = 5,
    EXC_IRQ = 6,
    EXC_FIQ = 7
} exception_type;

void exception_dispatch(exception_type exctype, exception_frame_t *frame) {
    switch (exctype) {
        case (EXC_UNDEF): {
            KERROR("Undefined instruction (at pc=%x from lr=%x)", frame->fault_pc, frame->exc_lr);
            panic();
        }
        break;
        case (EXC_SVC): {
            KWARN("No support for supervisor calls (at pc=%x from lr=%x)", frame->fault_pc, frame->exc_lr);
        }
        break;
        case (EXC_PREFETCH_ABORT): {
            uint32_t ifar, ifsr;
            __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));   // IFAR
            __asm__ volatile("mrc p15, 0, %0, c5, c0, 2" : "=r"(ifsr));  // IFSR
            
            KERROR("Aborted on prefetch (at pc=%x from lr=%x)", frame->fault_pc, frame->exc_lr);
            panic();
        }
        break;
        case EXC_DATA_ABORT: {
            uint32_t far, dfsr;
            __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(far));   // FAR
            __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));  // DFSR
            
            KERROR("Data Abort: FAR=%08x DFSR=%08x PC=%08x LR=%08x",
                far, dfsr, frame->fault_pc, frame->exc_lr);
            
            // Decode DFSR status bits
            uint32_t status = (dfsr & 0xF) | ((dfsr >> 6) & 0x10);
            const char* fault_str = "Unknown";
            switch (status) {
                case 0x5: fault_str = "Translation fault (section)"; break;
                case 0x7: fault_str = "Translation fault (page)"; break;
                case 0x9: fault_str = "Domain fault (section)"; break;
                case 0xB: fault_str = "Domain fault (page)"; break;
                case 0xD: fault_str = "Permission fault (section)"; break;
                case 0xF: fault_str = "Permission fault (page)"; break;
            }
            KERROR("Fault type: %s, %s", fault_str, (dfsr & (1<<11)) ? "Write" : "Read");
            panic();
        }
        break;
        case (EXC_RESERVED): {
            KERROR("No support for reserved exception (at pc=%x from lr=%x)", frame->fault_pc, frame->exc_lr);
            panic();
        }
        break;
        case (EXC_IRQ): {
            KWARN("No support for interrupts (at pc=%x from lr=%x)", frame->fault_pc, frame->exc_lr);
        }
        break;
        case (EXC_FIQ): {
            KWARN("No support for fast interrupts (at pc=%x from lr=%x)", frame->fault_pc, frame->exc_lr);
        }
        break;
        default: {
            KERROR("Unknown exception occurred (at pc=%x from lr=%x)", frame->fault_pc, frame->exc_lr);
            panic();
        }
        break;
    }
}