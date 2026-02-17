#ifndef KPRINTF_H
#define KPRINTF_H

#include "drivers/uart/uart.h"
#include <stddef.h>

void kprintf_init(void (*putc_func)(char));

/**
 *
 * @brief: Kernel printf function for formatted output.
 * @param fmt The format string.
 * @param ... Additional arguments to be formatted.
 *
 */
void kprintf(const char* fmt, ...);

#ifdef STATS_MODE

#define KLOG_RING_LINES  10
#define KLOG_LINE_MAX    80

size_t klog_ring_count(void);
const char *klog_ring_line(size_t index);

#endif

#endif