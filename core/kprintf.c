#include "core/kprintf.h"
#include "core/log.h"
#include "lib/string.h"
#include "core/assert.h"

static void (*kernel_console_putc)(char);

void kprintf_init(void (*putc_func)(char)) {
    kassert(putc_func != NULL);
    kernel_console_putc = putc_func;
}

void kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vstrfmt(kernel_console_putc, fmt, args);
    va_end(args);
}