#ifndef UART_H
#define UART_H

#include <stddef.h>
#include <stdint.h>
#include "../lib/string.h"

#define UART0_BASE 0x1C090000UL // UART address for vexpress-a15


typedef enum {
    UART_FAIL = 2,
    UART_BUSY = 1,
    UART_OK = 0
} UART_CODE;

static inline int uart_busy(void) {
    volatile uint32_t *UART0_FR = (volatile uint32_t *)(UART0_BASE + 0x18);
    return (*UART0_FR & 0b00100000) ? 1 : 0;
}

/**
 * @brief Send a single character over UART.
 * @param c Character to send.
 * @return UART_SUCCESS if transmitted, UART_BUSY if TX FIFO full, UART_ERR otherwise.
 */
UART_CODE uart_putc(char c);

/**
 * @brief Send a null-terminated string over UART.
 * @param string Pointer to string.
 * @return UART_SUCCESS on full transmission, UART_ERR on failure.
 */
UART_CODE uart_puts(const char *string);


/**
 * @brief Send a null-terminated string over UART.
 * @param string Pointer to string.
 * @return UART_SUCCESS on full transmission, UART_ERR on failure.
 */
UART_CODE uart_printf(const char *fstring);

#endif