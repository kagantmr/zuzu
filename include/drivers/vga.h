#ifndef VGA_H
#define VGA_H

#include <stddef.h>
#include <stdint.h>
#include "../lib/string.h"

#define UART0_BASE 0x1C090000UL // UART address for vexpress-a15

#define UART_FAIL 2
#define UART_OK 0
#define UART_BUSY 1

static inline int uart_busy(void) {
    volatile uint32_t *UART0_FR = (volatile uint32_t *)(UART0_BASE + 0x18);
    return (*UART0_FR & 0b00100000) ? 1 : 0;
}

/**
 * @brief Send a single character over VGA.
 * @param c Character to send.
 */
void vga_putc(char c);

/**
 * @brief Send a null-terminated string over VGA.
 * @param string Pointer to string.
 * @return UART_SUCCESS on full transmission, UART_FAIL on failure.
 */
int vga_puts(const char *string);


/**
 * @brief Send formatted string over VGA.
 * @param string Pointer to string.
 * @return UART_SUCCESS on full transmission, UART_FAIL on failure.
 */
int vga_printf(const char *fstring, ...);

#endif