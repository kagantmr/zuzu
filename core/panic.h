#ifndef PANIC_H
#define PANIC_H

#include <arch/regs.h>
#include <stdint.h>

/*
 * Fault context filled by exception handlers before calling panic().
 * When valid != 0 the panic renderer shows a FAULT section.
 */
typedef struct {
    int valid;
    uint32_t far;              /* DFAR or IFAR */
    uint32_t fsr;              /* DFSR or IFSR */
    const char *fault_type;    /* "Data abort" / "Prefetch abort" / etc. */
    const char *fault_decoded; /* DecodeFsr() result */
    const char *access_type;   /* "Read" / "Write" */
    CpuState *frame;           /* saved registers at exception entry */
} PanicFaultContext;

extern PanicFaultContext panic_fault_ctx;

/*
 * Halt the kernel to prevent further damage, and provide debugging information about the issue.
 *
 * The reason is a printf-style format string (rendered with vsnprintf,
 * truncated to one screen line).
 *
 * Disables interrupts, dumps FAULT / CPU STATE / BACKTRACE and optional
 * sections controlled by Makefile flags, then spins in WFI.
 * Uses polled UART only.
 *
 * Optional sections (default all on, toggled in core/Kconfig):
 *   CONFIG_PANIC_SECTION_PROCESS    current Space, handles, trapframe, IPC
 *   CONFIG_PANIC_SECTION_SCHEDULER  run queue, sleep queue
 *   CONFIG_PANIC_SECTION_IRQ        GIC enabled/pending lines, IRQ owners
 *   CONFIG_PANIC_SECTION_MEMORY     PMM, heap, kernel stack
 *
 * Does not return.
 */
_Noreturn void __attribute__((cold, format(printf, 1, 2))) panic(const char *fmt, ...);

#endif
