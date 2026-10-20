/**
 * @file kprintf.c
 * @brief Kernel printf implementation: formats under IRQ-off and streams to the console callback.
 */


#include "core/kprintf.h"
#include <arch/cpu.h>
#include <snprintf.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief Character output callback installed by kprintf_init(); NULL until set. */
static void (*kernel_console_putc)(char);

void kprintf_init(void (*putc_func)(char)) { kernel_console_putc = putc_func; }

/**
 * @brief Sink adapter passed to vstrfmt() that forwards one character to the console callback.
 *
 * @param[in] ctx Console putc callback, cast to a void pointer; may be NULL, in which case the character is dropped.
 * @param[in] c Character to emit.
 */
static void KprintfOutc(void *ctx, char c)
{
    void (*putc_fn)(char) = (void (*)(char))ctx;
    if (putc_fn)
        putc_fn(c);
}

void kprintf(const char *fmt, ...)
{
    uint32_t state = ArchIrqSave();
    va_list args;
    va_start(args, fmt);
    vstrfmt(KprintfOutc, (void *)kernel_console_putc, fmt, &args);
    va_end(args);
    ArchIrqRestore(state);
}
