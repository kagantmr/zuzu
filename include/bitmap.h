
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

/* First index starting a run of `run` consecutive zero bits, or -1. Bit-at-a-
 * time scan -- fine for the small bitmaps that need contiguous runs. */
static inline int BitmapFindClearRun(const uint32_t *words, const size_t nbits, const size_t run)
{
    if (run == 0 || run > nbits)
        return -1;
    size_t count = 0;
    for (size_t i = 0; i < nbits; i++)
    {
        if (BitmapTest(words, i))
        {
            count = 0;
            continue;
        }
        if (++count == run)
            return (int)(i + 1 - run);
    }
    return -1;
}

static inline void BitmapSetRange(uint32_t *words, const size_t start, const size_t count)
{
    for (size_t i = 0; i < count; i++)
        BitmapSet(words, start + i);
}

static inline void BitmapClrRange(uint32_t *words, const size_t start, const size_t count)
{
    for (size_t i = 0; i < count; i++)
        BitmapClr(words, start + i);
}

#endif