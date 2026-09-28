#ifndef FNV1A_H
#define FNV1A_H

#include <stdint.h>

static inline uint32_t Fnv1aHash(const char *s)
{
    uint32_t h = 0x811c9dc5U; // FNV offset basis
    while (*s)
    {
        h ^= (uint8_t)*s++;
        h *= 0x01000193U; // FNV prime
    }
    return h;
}

#define LABEL_OF(s) Fnv1aHash(s)


#endif /* FNV1A_H  */
