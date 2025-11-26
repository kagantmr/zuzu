#include "lib/mem.h"


void *memcpy(void *dest, const void *src, size_t n) {
    void *result = dest;

    unsigned char *d = (unsigned char *) dest;
    const unsigned char *s = (const unsigned char *) src;

    for (size_t i = 0; i < n; i++) {
        *d++ = *s++;
    }

    return result;
}


void *memset(void *ptr, char x, size_t n) {
    void *result = ptr;

    unsigned char *p = (unsigned char *) ptr;

    for (size_t bytes_copied = 0; bytes_copied < n; bytes_copied++) {
        *p++ = x;
    }
    
    return result;
}


void *memmove(void *dest, const void *src, size_t n) {
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

uintptr_t align_up(uintptr_t addr, size_t alignment) {
    if (alignment == 0) {
        return addr;
    }

    uintptr_t remainder = addr % alignment;
    if (remainder == 0) {
        return addr;
    }

    return addr + (alignment - remainder);
}