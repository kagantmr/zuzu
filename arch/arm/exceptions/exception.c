
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
            KERROR("Aborted on prefetch (at pc=%x from lr=%x)", frame->fault_pc, frame->exc_lr);
            panic();
        }
        break;
        case (EXC_DATA_ABORT): {
            KERROR("Bad access (at pc=%x from lr=%x)", frame->fault_pc, frame->exc_lr);
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