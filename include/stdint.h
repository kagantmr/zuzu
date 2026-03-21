#ifndef ZUZU_STDINT_H
#define ZUZU_STDINT_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint32_t           uintptr_t;
typedef int32_t            intptr_t;

#define UINT32_MAX 0xFFFFFFFF
#define UINTPTR_MAX UINT32_MAX
#define INT32_MAX 0x7FFFFFFF
#define INT32_MIN (-INT32_MAX - 1)

#endif