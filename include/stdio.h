#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "snprintf.h"


int printf(const char *format, ...);
int vprintf(const char *format, va_list args);
int sprintf(char *buf, const char *format, ...);
int vsprintf(char *buf, const char *format, va_list args);
int stdio_register_zuart(void);


#endif 