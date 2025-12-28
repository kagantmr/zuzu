#include <stdint.h>
typedef struct exception_frame {
    uint32_t spsr;          // [0]
    uint32_t r[13];         // [1..13] r0..r12
    uint32_t fault_pc;      // [14] modified lr (lr-4 or lr-8)
    uint32_t exc_lr;        // [15] original exception lr (return address)
} exception_frame_t;