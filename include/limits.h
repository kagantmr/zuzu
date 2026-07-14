#ifndef LIMITS_H
#define LIMITS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

#define CHAR_BIT 8

#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255

#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX

#define SHRT_MIN (-32767-1)
#define SHRT_MAX 32767
#define USHRT_MAX 65535U

#define INT_MIN INT32_MIN
#define INT_MAX INT32_MAX
#define UINT_MAX UINT32_MAX

#define LONG_MIN INT32_MIN
#define LONG_MAX INT32_MAX
#define ULONG_MAX UINT32_MAX

#define LLONG_MIN (-0x7FFFFFFFFFFFFFFFLL - 1)
#define LLONG_MAX 0x7FFFFFFFFFFFFFFFLL
#define ULLONG_MAX 0xFFFFFFFFFFFFFFFFULL

#ifdef __cplusplus
}
#endif

#endif /* LIMITS_H */
