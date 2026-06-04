#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>
#include "arch/arm/include/context.h"

/*
 * Fault context filled by exception handlers before calling panic().
 * When valid != 0 the panic renderer shows a FAULT section.
 */
typedef struct {
    int valid;
    uint32_t far;               /* DFAR or IFAR */
    uint32_t fsr;               /* DFSR or IFSR */
    const char *fault_type;     /* "Data abort" / "Prefetch abort" / etc. */
    const char *fault_decoded;  /* decode_fault_status() result */
    const char *access_type;    /* "Read" / "Write" */
    exception_frame_t *frame;   /* saved registers at exception entry */
} panic_fault_context_t;

extern panic_fault_context_t panic_fault_ctx;

/*
 * Halt the kernel with a structured diagnostic screen.
 *
 * Disables interrupts, dumps FAULT / CPU STATE / BACKTRACE and optional
 * sections controlled by Makefile flags, then spins in WFI.
 * Uses polled UART only — safe after heap / scheduler corruption.
 *
 * Optional sections (default all on, disable by passing 0 to make):
 *   PANIC_SECTION_PROCESS    current process, handles, trapframe, IPC
 *   PANIC_SECTION_SCHEDULER  run queue, sleep queue
 *   PANIC_SECTION_IRQ        GIC enabled/pending lines, IRQ owners
 *   PANIC_SECTION_MEMORY     PMM, heap, kernel stack
 *
 * Does not return.
 */
_Noreturn void __attribute__((cold)) panic(const char *reason);

#endif
