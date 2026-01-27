#ifndef UART_PL011_H
#define UART_PL011_H

#include <stdint.h>
#include <stdbool.h>

#include "drivers/uart/uart.h"

#define PL011_DR    0x00
#define PL011_FR    0x18
#define PL011_IBRD  0x24
#define PL011_FBRD  0x28
#define PL011_LCRH  0x2C
#define PL011_CR    0x30
#define PL011_IMSC  0x38   // Interrupt Mask Set/Clear
#define PL011_MIS   0x40   // Masked Interrupt Status
#define PL011_ICR   0x44

#define IMSC_RXIM   (1u << 4)   // RX interrupt mask

#define UART_RX_BUF_SIZE 256

#define FR_TXFF     (1u << 5)
#define FR_RXFE     (1u << 4) 

#define LCRH_FEN    (1u << 4)
#define LCRH_WLEN_8 (3u << 5)

#define CR_UARTEN   (1u << 0)
#define CR_TXE      (1u << 8)
#define CR_RXE      (1u << 9)

void pl011_init(uintptr_t base_addr);
void pl011_init_irq(uintptr_t base_addr);
void pl011_putc(char c);
int  pl011_puts(const char *string);
void pl011_putc(char c);                   // Always polled (fast enough)
int  pl011_getc(void);                     // Returns -1 if no char available
bool pl011_has_rx(void);                   // Check if char available

extern const struct uart_driver pl011_driver;

#endif