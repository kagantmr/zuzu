/**
 * @file panic.h
 * @brief Kernel panic entry point and the fault context that fault handlers hand to it.
 */
#ifndef PANIC_H
#define PANIC_H

#include <arch/regs.h>
#include <stdint.h>

/** @brief Fault details recorded by an exception handler so the panic screen can print them. */
typedef struct {
    int valid;                  /**< Non-zero when the remaining fields describe a real fault. */
    uint32_t far;               /**< Fault address register value. */
    uint32_t fsr;               /**< Fault status register value. */
    const char *fault_type;     /**< Short fault class name, or NULL. */
    const char *fault_decoded;  /**< Human-readable decoding of the status, or NULL. */
    const char *access_type;    /**< Kind of access that faulted (for example read or write), or NULL. */
    CpuState *frame;            /**< Saved CPU state of the faulting context, or NULL. */
} PanicFaultContext;

/** @brief Fault context consumed by panic(); handlers fill it in before calling panic(). */
extern PanicFaultContext panic_fault_ctx;

/**
 * @brief Halt the kernel with a diagnostic screen.
 *
 * Disables interrupts, formats the reason, prints the panic screen (reason, context, fault, CPU
 * state, backtrace and the enabled optional sections) on the UART and halts the CPU. A nested
 * panic skips the report and halts immediately. Never returns.
 *
 * @param[in] fmt printf-style format string for the reason; NULL prints "unknown".
 * @param[in] ... Arguments for @p fmt.
 */
_Noreturn void __attribute__((cold, format(printf, 1, 2))) panic(const char *fmt, ...);

#endif
