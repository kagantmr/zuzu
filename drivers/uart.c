#include "drivers/uart.h"
#include "lib/string.h"
#include "core/log.h"
#include "core/assert.h"
#include <stddef.h>
#include <stdint.h>

uintptr_t UART0_BASE; // Default UART0 base address

static inline int uart_tx_full(void) {
    volatile uint32_t *UART0_FR = (volatile uint32_t *)(UART0_BASE + 0x18);
    return (*UART0_FR & 0b00100000) ? 1 : 0;
}

void uart_init(uintptr_t base_addr) {
    // UART initialization code can be added here if needed
    kassert(base_addr != 0);
    UART0_BASE = base_addr;
}

void uart_putc(char c) {
    kassert(UART0_BASE != 0);
    volatile uint32_t *UART0 = (uint32_t *)UART0_BASE;
    while (uart_tx_full());
    *UART0 = c;
}

int uart_puts(const char *string) {
    kassert(string != NULL);
    while (*string) {
        uart_putc(*string);
        string++;
    }
    return UART_OK;
}

int uart_printf(const char *fstring, ...) {
    kassert(fstring != NULL);
    va_list list;
    va_start(list, fstring);
    vstrfmt(uart_putc, fstring, list);
    va_end(list);
    return UART_OK;
}