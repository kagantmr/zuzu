
#ifndef BITMAP_H
#define BITMAP_H

#include <stddef.h>
#include <stdint.h>

#define BITMAP_WORDS(nbits) (((nbits) + 31) / 32)

static inline void BitmapZero(uint32_t *words, const size_t nbits)
{
    for (size_t w = 0; w < BITMAP_WORDS(nbits); w++)
        words[w] = 0;
}

static inline void BitmapSet(uint32_t *words, const size_t i) { words[i / 32] |= 1U << (i % 32); }

static inline void BitmapClr(uint32_t *words, const size_t i)
{
    words[i / 32] &= ~(1U << (i % 32));
}

static inline int BitmapTest(const uint32_t *words, const size_t i)
{
    return ((words[i / 32] >> (i % 32)) & 1U) ? 1 : 0;
}

static inline int BitmapFindFirstZero(const uint32_t *words, const size_t nbits)
{
    const size_t full = nbits / 32;
    for (size_t w = 0; w < full; w++)
    {
        if (words[w] != 0xFFFFFFFFU)
            return (int)((w * 32) + (size_t)__builtin_ctz(~words[w]));
    }
    const size_t rem = nbits % 32;
    if (rem)
    {
        const uint32_t free = ~words[full] & ((1U << rem) - 1U);
        if (free)
            return (int)((full * 32) + (size_t)__builtin_ctz(free));
    }
    return -1;
}

#endif