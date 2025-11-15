#include "drivers/uart.h"
#include "lib/string.h"

#define UART_CHAR_MAX 256


void uart_putc(char c) {
    volatile uint32_t *UART0 = (uint32_t *)UART0_BASE;
    while (uart_busy());
    *UART0 = c;
}

int uart_puts(const char *string) {
    size_t len = strlen(string);
    if (len > UART_CHAR_MAX) {
        return UART_FAIL;
    }
    while (*string) {
        uart_putc(*string);
        string++;
    }
    return UART_OK;
}

int uart_printf(const char *fstring, ...) {
    va_list list;
    va_start(list, fstring);
    vstrfmt(uart_putc, fstring, list);
    va_end(list);
    return UART_OK;
}