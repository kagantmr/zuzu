/**
 * @file
 * @brief 32-bit FNV-1a string hash.
 */

#ifndef FNV1A_H
#define FNV1A_H

#include <stdint.h>

/**
 * @brief Hashes a NUL-terminated string with 32-bit FNV-1a.
 *
 * Fast and well spread, not cryptographic.
 *
 * @param[in] s String to hash.
 * @return The hash.
 */
static inline uint32_t Fnv1aHash(const char *s)
{
    uint32_t h = 0x811c9dc5U;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 0x01000193U;
    }
    return h;
}

/** @brief Hash of a string literal, for use as a label. */
#define LABEL_OF(s) Fnv1aHash(s)

#endif /* FNV1A_H  */
