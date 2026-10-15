#ifndef PANIC_H
#define PANIC_H

#include <arch/regs.h>
#include <stdint.h>

typedef struct {
    int valid;
    uint32_t far;
    uint32_t fsr;
    const char *fault_type;
    const char *fault_decoded;
    const char *access_type;
    CpuState *frame;
} PanicFaultContext;

extern PanicFaultContext panic_fault_ctx;

_Noreturn void __attribute__((cold, format(printf, 1, 2))) panic(const char *fmt, ...);

#endif
