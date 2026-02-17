#ifndef PANIC_H
#define PANIC_H

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