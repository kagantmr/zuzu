#ifndef ZUZU_H
#define ZUZU_H

#include <stddef.h>

static inline void _quit(int status)
{
    register int r0 __asm__("r0") = status;
    __asm__ volatile("svc #0" : : "r"(r0));
}

static inline void _yield(void)
{
    __asm__ volatile(
        "svc #1\n" /* SYS_YIELD */
    );
}

/*
static inline void _spawn(const char* name, size_t len)
{
    register const char *r0 __asm__("r0") = name;
    register size_t r1 __asm__("r1") = len;
    __asm__ volatile("svc #3" : : "r"(r0), "r"(r1));
}*/

static inline void _log(const char* str, size_t len)
{
    register const char *r0 __asm__("r0") = str;
    register size_t r1 __asm__("r1") = len;
    __asm__ volatile("svc #0xF0" : : "r"(r0), "r"(r1));
}

#endif