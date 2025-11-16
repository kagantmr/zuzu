#include "drivers/uart.h"
#include "lib/string.h"
#include "core/log.h"
#include <stddef.h>
#include <stdint.h>

static inline int uart_tx_full(void) {
    volatile uint32_t *UART0_FR = (volatile uint32_t *)(UART0_BASE + 0x18);
    return (*UART0_FR & 0b00100000) ? 1 : 0;
}

void uart_init(void) {
    // UART initialization code can be added here if needed
}

void uart_putc(char c) {
    volatile uint32_t *UART0 = (uint32_t *)UART0_BASE;
    while (uart_tx_full());
    *UART0 = c;
}

int uart_puts(const char *string) {
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