#include "lib/mem.h"
#include "kernel/mm/pmm.h"
#include "core/assert.h"

uint32_t bswap32(uint32_t x) {
#ifdef __GNUC__
    return __builtin_bswap32(x);
#else
    return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) |
           ((x & 0x00FF0000u) >> 8)  | ((x & 0xFF000000u) >> 24);
#endif
}

void *memcpy(void *dest, const void *src, size_t n) {
    kassert(n == 0 || (dest != NULL && src != NULL));
    void *result = dest;

    unsigned char *d = (unsigned char *) dest;
    const unsigned char *s = (const unsigned char *) src;

    for (size_t i = 0; i < n; i++) {
        *d++ = *s++;
    }

    return result;
}


void *memset(void *ptr, char x, size_t n) {
    kassert(n == 0 || (ptr != NULL));
    void *result = ptr;

    unsigned char *p = (unsigned char *) ptr;

    for (size_t bytes_copied = 0; bytes_copied < n; bytes_copied++) {
        *p++ = x;
    }
    
    return result;
}


void *memmove(void *dest, const void *src, size_t n) {
    kassert(n == 0 || (dest != NULL && src != NULL));
    void *result = dest;

    unsigned char *d = (unsigned char *) dest;
    const unsigned char *s = (const unsigned char *) src;

    if (d < s) {
        for (size_t bytes_copied = 0; bytes_copied < n; bytes_copied++) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += n - 1;
        s += n - 1;
        for (size_t bytes_copied = 0; bytes_copied < n; bytes_copied++) {
            *d-- = *s--;
        }
    } else {
        return result;
    }

    return result;
}

uintptr_t align_down(uintptr_t addr, size_t alignment) {
    kassert(alignment != 0);
    kassert((alignment & (alignment - 1)) == 0); // power of two
    return addr & ~(alignment - 1);
}

uintptr_t align_up(uintptr_t addr, size_t alignment) {
    kassert(alignment != 0);
    kassert((alignment & (alignment - 1)) == 0); // power of two

    uintptr_t remainder = addr % alignment;
    if (remainder == 0) {
        return addr;
    }

    return addr + (alignment - remainder);
}


