#ifndef VGA_H
#define VGA_H


#define UART0_BASE 0x1C090000UL // UART address for vexpress-a15

#define UART_FAIL 2
#define UART_OK 0
#define UART_BUSY 1

/**
 * @brief Initialize the VGA hardware.
 */
void vga_init(void);

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