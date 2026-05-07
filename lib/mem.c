#include "mem.h"

void *memcpy(void *dest, const void *src, size_t n)
{
    void *result = dest;

    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    for (size_t i = 0; i < n; i++)
    {
        *d++ = *s++;
    }

    return result;
}

void *memset(void *ptr, char x, size_t n)
{
    void *result = ptr;

    unsigned char *p = (unsigned char *)ptr;

    for (size_t bytes_copied = 0; bytes_copied < n; bytes_copied++)
    {
        *p++ = x;
    }

    return result;
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