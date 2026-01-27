#ifndef UART_H
#define UART_H

#include <stdint.h>

#define UART_FAIL 2
#define UART_OK 0
#define UART_BUSY 1

struct uart_driver {
    void (*init)(uintptr_t base_addr);
    void (*putc)(char c);
    int  (*puts)(const char *string); // Optional, falls back to putc loop
    int  (*getc)(void);               // Optional, returns -1 if no char
};

void uart_set_driver(const struct uart_driver *driver, uintptr_t base_addr);
const struct uart_driver *uart_get_driver(void);
uintptr_t uart_get_base(void);

/**
 * @brief Initialize the UART hardware.
 */
void uart_init(uintptr_t base_addr);

/**
 * @brief Swap the UART base address to a new one.
 * @param new_base_addr New base address for UART.
 */
static inline void uart_swap(uintptr_t new_base_addr) {
    uart_init(new_base_addr);
}

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