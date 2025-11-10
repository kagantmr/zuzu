#include "drivers/uart.h"
#include "lib/string.h"

#define UART_CHAR_MAX 256


UART_CODE uart_putc(char c) {
    volatile uint32_t *UART0 = (uint32_t *)UART0_BASE;
    while (uart_busy());
    *UART0 = c;
    return UART_OK;
}

UART_CODE uart_puts(const char *string) {
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