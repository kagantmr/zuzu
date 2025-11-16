#ifndef UART_H
#define UART_H

#define UART0_BASE 0x1C090000UL // UART address for vexpress-a15

#define UART_FAIL 2
#define UART_OK 0
#define UART_BUSY 1

/**
 * @brief Initialize the UART hardware.
 */
void uart_init(void);


/**
 * @brief Send a single character over UART.
 * @param c Character to send.
 */
void uart_putc(char c);

/**
 * @brief Send a null-terminated string over UART.
 * @param string Pointer to string.
 * @return UART_OK on full transmission, UART_FAIL on failure.
 */
int uart_puts(const char *string);


/**
 * @brief Send formatted string over UART.
 * @param string Pointer to string.
 * @return UART_OK on full transmission, UART_FAIL on failure.
 */
int uart_printf(const char *fstring, ...);

#endif