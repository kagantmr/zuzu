#ifndef UART_PL011_H
#define UART_PL011_H

#include <stdint.h>

#include "drivers/uart/uart.h"

void pl011_init(uintptr_t base_addr);
void pl011_putc(char c);
int  pl011_puts(const char *string);

extern const struct uart_driver pl011_driver;

#endif