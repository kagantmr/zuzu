/**
 * @file mem.c
 * @brief Plain C memcpy() and memset() for AArch64, until hand-written versions replace them.
 */

#include "string.h"

__attribute__((used, optimize("no-tree-loop-distribute-patterns"))) void *
memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dest;
}

__attribute__((used, optimize("no-tree-loop-distribute-patterns"))) void *memset(void *s, int c,
                                                                                  size_t n)
{
    unsigned char *p = s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}
