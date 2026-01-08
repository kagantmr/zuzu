#ifndef KPRINTF_H
#define KPRINTF_H

#include "drivers/uart/uart.h"

void kprintf_init(void (*putc_func)(char));

/**
 * 
 * @brief: Kernel printf function for formatted output.
 * @param fmt The format string.
 * @param ... Additional arguments to be formatted.
 * 
 */
void kprintf(const char* fmt, ...);

#endif