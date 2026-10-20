/**
 * @file
 * @brief Fixed-size bitmaps stored in 32-bit words.
 */

#ifndef BITMAP_H
#define BITMAP_H

#include <stddef.h>
#include <stdint.h>

/** @brief Words needed for a bitmap of @p nbits bits. */
#define BITMAP_WORDS(nbits) (((nbits) + 31) / 32)

/**
 * @brief Clears every bit.
 *
 * @param[out] words Bitmap.
 * @param[in]  nbits Bits in the bitmap.
 */
static inline void BitmapZero(uint32_t *words, const size_t nbits)
{
    for (size_t w = 0; w < BITMAP_WORDS(nbits); w++)
        words[w] = 0;
}

/**
 * @brief Sets one bit.
 *
 * @param[in,out] words Bitmap.
 * @param[in]     i     Bit index.
 */
static inline void BitmapSet(uint32_t *words, const size_t i) { words[i / 32] |= 1U << (i % 32); }

/**
 * @brief Clears one bit.
 *
 * @param[in,out] words Bitmap.
 * @param[in]     i     Bit index.
 */
static inline void BitmapClr(uint32_t *words, const size_t i)
{
    words[i / 32] &= ~(1U << (i % 32));
}

/**
 * @brief Reads one bit.
 *
 * @param[in] words Bitmap.
 * @param[in] i     Bit index.
 * @return 1 if set, 0 if clear.
 */
static inline int BitmapTest(const uint32_t *words, const size_t i)
{
    return ((words[i / 32] >> (i % 32)) & 1U) ? 1 : 0;
}

/**
 * @brief Finds the lowest clear bit.
 *
 * @param[in] words Bitmap.
 * @param[in] nbits Bits in the bitmap.
 * @return Its index, or -1 if every bit is set.
 */
static inline int BitmapFindFirstZero(const uint32_t *words, const size_t nbits)
{
    const size_t full = nbits / 32;
    for (size_t w = 0; w < full; w++) {
        if (words[w] != 0xFFFFFFFFU)
            return (int)((w * 32) + (size_t)__builtin_ctz(~words[w]));
    }
    const size_t rem = nbits % 32;
    if (rem) {
        const uint32_t free = ~words[full] & ((1U << rem) - 1U);
        if (free)
            return (int)((full * 32) + (size_t)__builtin_ctz(free));
    }
    return -1;
}

/**
 * @brief Finds the first run of @p run consecutive clear bits, scanning a bit at a time.
 *
 * @param[in] words Bitmap.
 * @param[in] nbits Bits in the bitmap.
 * @param[in] run   Length of the run.
 * @return Index of the run's first bit, or -1 if there is none or @p run is 0 or too long.
 */
static inline int BitmapFindClearRun(const uint32_t *words, const size_t nbits, const size_t run)
{
    if (run == 0 || run > nbits)
        return -1;
    size_t count = 0;
    for (size_t i = 0; i < nbits; i++) {
        if (BitmapTest(words, i)) {
            count = 0;
            continue;
        }
        if (++count == run)
            return (int)(i + 1 - run);
    }
    return -1;
}

/**
 * @brief Sets @p count bits starting at @p start.
 *
 * @param[in,out] words Bitmap.
 * @param[in]     start First bit.
 * @param[in]     count Number of bits.
 */
static inline void BitmapSetRange(uint32_t *words, const size_t start, const size_t count)
{
    for (size_t i = 0; i < count; i++)
        BitmapSet(words, start + i);
}

/**
 * @brief Clears @p count bits starting at @p start.
 *
 * @param[in,out] words Bitmap.
 * @param[in]     start First bit.
 * @param[in]     count Number of bits.
 */
static inline void BitmapClrRange(uint32_t *words, const size_t start, const size_t count)
{
    for (size_t i = 0; i < count; i++)
        BitmapClr(words, start + i);
}

#endif