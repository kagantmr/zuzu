#include "core/kprintf.h"
#include "core/log.h"
#include "arch/arm/include/irq.h"
#include "lib/string.h"

static void (*kernel_console_putc)(char);

void kprintf_init(void (*putc_func)(char)) {
    kernel_console_putc = putc_func;
}

void kprintf(const char* fmt, ...) {
    arch_global_irq_disable();
    va_list args;
    va_start(args, fmt);
    vstrfmt(kernel_console_putc, fmt, args);
    va_end(args);
    arch_global_irq_enable();
}