#include <stdint.h>

typedef struct exception_frame {
    uint32_t spsr;
    uint32_t r[13];
    uint32_t pc, lr;
} exception_frame;