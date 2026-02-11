#include <stdint.h>
typedef struct exception_frame {
    uint32_t r[13];       // [0..12]  r0-r12
    uint32_t lr;          // [13]     lr_svc
    uint32_t return_pc;   // [14]     where to return (adjusted lr)
    uint32_t return_cpsr; // [15]     saved CPSR
} exception_frame_t;