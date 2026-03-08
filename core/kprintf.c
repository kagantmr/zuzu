#include "core/kprintf.h"
#include "core/log.h"
#include "arch/arm/include/irq.h"
#include "lib/string.h"
#include "lib/snprintf.h"
#include <stdarg.h>
#include <stddef.h>

static void (*kernel_console_putc)(char);

void kprintf_init(void (*putc_func)(char)) {
    kernel_console_putc = putc_func;
}

void kprintf(const char* fmt, ...) {
    uint32_t state = arch_irq_save();

    va_list args;
    va_start(args, fmt);
    vstrfmt(kernel_console_putc, fmt, &args);
    va_end(args);

    arch_irq_restore(state);
}