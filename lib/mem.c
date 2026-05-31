#include "mem.h"

typedef struct { uint32_t w[8]; } __attribute__((aligned(4))) chunk32_t;

void *memcpy(void *dst, const void *src, size_t n) {
    uintptr_t d = (uintptr_t)dst, s = (uintptr_t)src;

    if (n >= 32 && ((d | s) & 3) == 0) {
        chunk32_t *dc = dst; const chunk32_t *sc = src;
        size_t chunks = n / 32;
        while (chunks--) *dc++ = *sc++;
        n &= 31;
        dst = dc; src = (const void *)sc;
    }
    // word loop 
    if (n >= 4 && (((uintptr_t)dst | (uintptr_t)src) & 3) == 0) {
        uint32_t *dw = dst; const uint32_t *sw = src;
        size_t words = n / 4;
        while (words--) *dw++ = *sw++;
        n &= 3;
        dst = dw; src = (const void *)sw;
    }
    uint8_t *db = dst; const uint8_t *sb = src;
    while (n--) *db++ = *sb++;
    return dst;
}


void *memset(void *dst, int c, size_t n) {
    uintptr_t d = (uintptr_t)dst;
    uint8_t b = (uint8_t)c;
    uint32_t fill = (uint32_t)b * 0x01010101u;
    if (n >= 32 && (d & 3) == 0) {
        chunk32_t *dc = dst;
        size_t chunks = n / 32;
        while (chunks--) {
            for (size_t i = 0; i < 8; ++i) {
                dc->w[i] = fill;
            }
            ++dc;
        }
        n &= 31;
        dst = dc;
    }
    if (n >= 4 && (((uintptr_t)dst) & 3) == 0) {
        uint32_t *dw = dst;
        size_t words = n / 4;
        while (words--) {
            *dw++ = fill;
        }
        n &= 3;
        dst = dw;
    }
    uint8_t *db = dst;
    while (n--) *db++ = b;
    return dst;
}

void *memmove(void *dest, const void *src, size_t n)
{
    void *result = dest;

    if (n == 0 || dest == src)
    {
        return result;
    }

    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s)
    {
        for (size_t bytes_copied = 0; bytes_copied < n; bytes_copied++)
        {
            *d++ = *s++;
        }
    }
    else if (d > s)
    {
        d += n - 1;
        s += n - 1;
        for (size_t bytes_copied = 0; bytes_copied < n; bytes_copied++)
        {
            *d-- = *s--;
        }
    }

    return result;
}

int memcmp(const void *str1, const void *str2, size_t count)
{
    const unsigned char *s1 = (const unsigned char *)str1;
    const unsigned char *s2 = (const unsigned char *)str2;

    while (count-- > 0)
    {
        if (*s1++ != *s2++)
            return s1[-1] < s2[-1] ? -1 : 1;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    while (n--) {
        if (*p == (unsigned char)c)
            return (void *)p;
        p++;
    }
    return NULL;
}