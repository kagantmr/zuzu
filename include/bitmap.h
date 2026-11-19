/**
 * @file
 * @brief Fixed-size bitmaps stored in machine words.
 */

#ifndef BITMAP_H
#define BITMAP_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** @brief One bitmap word: 32 bits on ARM32, 64 on AArch64. */
typedef unsigned long BitmapWord;

/** @brief Bits in one bitmap word. */
#define BITMAP_WORD_BITS (sizeof(BitmapWord) * 8)

/** @brief Words needed for a bitmap of @p nbits bits. */
#define BITMAP_WORDS(nbits) (((nbits) + (BITMAP_WORD_BITS - 1)) / BITMAP_WORD_BITS)

/**
 * @brief Clears every bit.
 *
 * @param[out] words Bitmap.
 * @param[in]  nbits Bits in the bitmap.
 */
static inline void BitmapZero(BitmapWord *words, const size_t nbits)
{
    memset(words, 0, BITMAP_WORDS(nbits) * sizeof(BitmapWord));
}

/**
 * @brief Sets one bit.
 *
 * @param[in,out] words Bitmap.
 * @param[in]     i     Bit index.
 */
static inline void BitmapSet(BitmapWord *words, const size_t i)
{
    words[i / BITMAP_WORD_BITS] |= (BitmapWord)1 << (i % BITMAP_WORD_BITS);
}

/**
 * @brief Clears one bit.
 *
 * @param[in,out] words Bitmap.
 * @param[in]     i     Bit index.
 */
static inline void BitmapClr(BitmapWord *words, const size_t i)
{
    words[i / BITMAP_WORD_BITS] &= ~((BitmapWord)1 << (i % BITMAP_WORD_BITS));
}

/**
 * @brief Reads one bit.
 *
 * @param[in] words Bitmap.
 * @param[in] i     Bit index.
 * @return 1 if set, 0 if clear.
 */
static inline int BitmapTest(const BitmapWord *words, const size_t i)
{
    return ((words[i / BITMAP_WORD_BITS] >> (i % BITMAP_WORD_BITS)) & 1U) ? 1 : 0;
}

/**
 * @brief Finds the lowest clear bit at or after @p from.
 *
 * @param[in] words Bitmap.
 * @param[in] nbits Bits in the bitmap.
 * @param[in] from  First bit to look at.
 * @return Its index, or @p nbits if there is none.
 */
static inline size_t BitmapNextZero(const BitmapWord *words, const size_t nbits, const size_t from)
{
    if (from >= nbits)
        return nbits;
    size_t w = from / BITMAP_WORD_BITS;
    const size_t last = (nbits - 1) / BITMAP_WORD_BITS;
    BitmapWord cur = ~words[w] & (~(BitmapWord)0 << (from % BITMAP_WORD_BITS));
    while (cur == 0) {
        if (++w > last)
            return nbits;
        cur = ~words[w];
    }
    const size_t i = (w * BITMAP_WORD_BITS) + (size_t)__builtin_ctzl(cur);
    return i < nbits ? i : nbits;
}

/**
 * @brief Finds the lowest set bit at or after @p from.
 *
 * @param[in] words Bitmap.
 * @param[in] nbits Bits in the bitmap.
 * @param[in] from  First bit to look at.
 * @return Its index, or @p nbits if there is none.
 */
static inline size_t BitmapNextSet(const BitmapWord *words, const size_t nbits, const size_t from)
{
    if (from >= nbits)
        return nbits;
    size_t w = from / BITMAP_WORD_BITS;
    const size_t last = (nbits - 1) / BITMAP_WORD_BITS;
    BitmapWord cur = words[w] & (~(BitmapWord)0 << (from % BITMAP_WORD_BITS));
    while (cur == 0) {
        if (++w > last)
            return nbits;
        cur = words[w];
    }
    const size_t i = (w * BITMAP_WORD_BITS) + (size_t)__builtin_ctzl(cur);
    return i < nbits ? i : nbits;
}

/**
 * @brief Finds the lowest clear bit.
 *
 * @param[in] words Bitmap.
 * @param[in] nbits Bits in the bitmap.
 * @return Its index, or @p nbits if every bit is set.
 */
static inline size_t BitmapFindFirstZero(const BitmapWord *words, const size_t nbits)
{
    return BitmapNextZero(words, nbits, 0);
}

/**
 * @brief Finds the first run of @p run consecutive clear bits whose start, plus @p phase, is a
 * multiple of @p align.
 *
 * @param[in] words Bitmap.
 * @param[in] nbits Bits in the bitmap.
 * @param[in] run   Length of the run.
 * @param[in] align Required alignment, a power of two; 1 for none.
 * @param[in] phase Offset added to a bit index before checking its alignment.
 * @return Index of the run's first bit, or @p nbits if there is none, @p run is 0 or too long, or
 * @p align is not a power of two.
 */
static inline size_t BitmapFindClearRun(const BitmapWord *words, const size_t nbits,
                                        const size_t run, const size_t align, const size_t phase)
{
    if (run == 0 || run > nbits || align == 0 || (align & (align - 1)) != 0)
        return nbits;
    size_t i = 0;
    for (;;) {
        size_t start = BitmapNextZero(words, nbits, i);
        start = ((start + phase + align - 1) & ~(align - 1)) - phase;
        if (start >= nbits || nbits - start < run)
            return nbits;
        const size_t end = BitmapNextSet(words, start + run, start);
        if (end - start >= run)
            return start;
        i = end;
    }
}

/**
 * @brief Sets @p count bits starting at @p start.
 *
 * @param[in,out] words Bitmap.
 * @param[in]     start First bit.
 * @param[in]     count Number of bits.
 */
static inline void BitmapSetRange(BitmapWord *words, size_t start, size_t count)
{
    for (; count > 0 && start % BITMAP_WORD_BITS; count--)
        BitmapSet(words, start++);
    for (; count >= BITMAP_WORD_BITS; count -= BITMAP_WORD_BITS, start += BITMAP_WORD_BITS)
        words[start / BITMAP_WORD_BITS] = ~(BitmapWord)0;
    for (; count > 0; count--)
        BitmapSet(words, start++);
}

/**
 * @brief Clears @p count bits starting at @p start.
 *
 * @param[in,out] words Bitmap.
 * @param[in]     start First bit.
 * @param[in]     count Number of bits.
 */
static inline void BitmapClrRange(BitmapWord *words, size_t start, size_t count)
{
    for (; count > 0 && start % BITMAP_WORD_BITS; count--)
        BitmapClr(words, start++);
    for (; count >= BITMAP_WORD_BITS; count -= BITMAP_WORD_BITS, start += BITMAP_WORD_BITS)
        words[start / BITMAP_WORD_BITS] = 0;
    for (; count > 0; count--)
        BitmapClr(words, start++);
}

#endif
