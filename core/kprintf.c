#include "core/kprintf.h"
#include "core/log.h"
#include "arch/arm/include/irq.h"
#include <string.h>
#include <snprintf.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

static void (*kernel_console_putc)(char);

void kprintf_init(void (*putc_func)(char)) {
    kernel_console_putc = putc_func;
}

static void kprintf_outc(void *ctx, char c) {
    void (*putc_fn)(char) = (void (*)(char))ctx;
    if (putc_fn) putc_fn(c);
}

void kprintf(const char* fmt, ...) {
    uint32_t state = arch_irq_save();
    va_list args;
    va_start(args, fmt);
    vstrfmt(kprintf_outc, (void *)kernel_console_putc, fmt, &args);
    va_end(args);
    arch_irq_restore(state);
}