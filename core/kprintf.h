#ifndef KPRINTF_H
#define KPRINTF_H

#include <stddef.h>

void kprintf_init(void (*putc_func)(char));

void kprintf(const char *fmt, ...);

#endif
