#include "drivers/uart/uart.h"
#include "lib/string.h"
#include "core/log.h"
#include "core/assert.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

static const struct uart_driver *active_driver;
static uintptr_t active_base;

static inline void ensure_driver(void) {
    kassert(active_driver != NULL);
}

const struct uart_driver *uart_get_driver(void) {
    return active_driver;
}

uintptr_t uart_get_base(void) {
    return active_base;
}

void uart_set_driver(const struct uart_driver *driver, uintptr_t base_addr) {
    kassert(driver != NULL);
    kassert(driver->init != NULL);
    kassert(driver->putc != NULL);
    kassert(base_addr != 0);

    active_driver = driver;
    active_base = base_addr;
    active_driver->init(base_addr);
}

void uart_init(uintptr_t base_addr) {
    ensure_driver();
    kassert(base_addr != 0);
    active_base = base_addr;
    active_driver->init(base_addr);
}

void uart_putc(char c) {
    ensure_driver();
    active_driver->putc(c);
}

int uart_puts(const char *string) {
    ensure_driver();
    kassert(string != NULL);

    if (active_driver->puts != NULL) {
        return active_driver->puts(string);
    }

    while (*string) {
        active_driver->putc(*string++);
    }
    return UART_OK;
}

int uart_printf(const char *fstring, ...) {
    ensure_driver();
    kassert(fstring != NULL);
    va_list list;
    va_start(list, fstring);
    vstrfmt(active_driver->putc, fstring, list);
    va_end(list);
    return UART_OK;
}