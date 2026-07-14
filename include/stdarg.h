#ifndef STDARG_H
#define STDARG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dest, src) __builtin_va_copy(dest, src)

#ifdef __cplusplus
}
#endif

#endif /* STDARG_H */