#include <stdint.h>

typedef struct exception_frame {
    uint32_t r[13];        // r0-r12

    uint32_t sp_usr;       // user SP saved via SRS
    uint32_t lr_usr;       // user LR saved via SRS

    uint32_t return_pc;    // adjusted return address (LR - offset)
    uint32_t return_cpsr;  // saved CPSR/SPSR value you return with
} exception_frame_t;