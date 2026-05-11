#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "snprintf.h"

#ifndef EOF
#define EOF (-1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

int printf(const char *format, ...);
int vprintf(const char *format, va_list args);
int sprintf(char *buf, const char *format, ...);
int vsprintf(char *buf, const char *format, va_list args);
int getchar(void);
int scanf(const char *format, ...);
int vscanf(const char *format, va_list args);
int stdio_register_zuart(void);
int stdio_route_tty(const char name[4]);
int stdio_use_tty(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif
