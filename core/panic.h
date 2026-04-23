/**
 * panic.h - Kernel panic handling and panic screen rendering
 * 
 * Kernel panics occur when an unrecoverable error happens in the kernel, such as
 * corruption of kernel data structures, fatal hardware faults, or failed assertions, 
 * as well as explicit calls to panic() for critical errors. The panic handler disables interrupts,
 * gathers diagnostic information, and displays a structured panic screen to help with debugging.
 * The panic screen includes a backtrace, process information, and memory state.
 */
#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>
#include "arch/arm/include/context.h"

/**
 * @brief Fault context filled by exception handlers before calling panic.
 *
 * If valid != 0, the panic renderer displays a FAULT DETAILS section
 * with the captured register and decoded fault information.
 */
typedef struct {
    int valid;
    uint32_t far;                 // DFAR or IFAR
    uint32_t fsr;                 // DFSR or IFSR
    const char *fault_type;       // "Data abort" / "Prefetch abort" / etc.
    const char *fault_decoded;    // decode_fault_status() result
    const char *access_type;      // "Read" / "Write"
    exception_frame_t *frame;     // saved registers
} panic_fault_context_t;

extern panic_fault_context_t panic_fault_ctx;

/**
 * @brief Handle a kernel panic by disabling interrupts and halting.
 *
 * Displays a structured panic screen with backtrace, process info,
 * and memory state. Uses polled UART only — no heap, no interrupts.
 *
 * Controlled by PANIC_FULL_SCREEN:
 *   1 = box-drawn screen with logo (default)
 *   0 = compact minimal output (safest)
 *
 * This function does not return.
 */
_Noreturn void panic(const char *reason);

#endif