/**
 * @file kprintf.h
 * @brief Kernel printf function
 */

#ifndef KPRINTF_H
#define KPRINTF_H

#include <stddef.h>

/**
 * @brief Initialize kprintf with a character stream callback.
 * 
 * @param[in] putc_func The character stream callback.
 */
void kprintf_init(void (*putc_func)(char));

/**
 * @brief Print a message through the set character stream.
 * 
 * @param[in] fmt The format string.
 */ 
void kprintf(const char *fmt, ...);

#endif
