/**
 * @file
 * @brief Number and string conversion, and byte-order helpers.
 */

#ifndef CONVERT_H
#define CONVERT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Convert a hexadecimal string to an integer.
 *
 * Skips leading whitespace, accepts a sign, and stops at the first non-hex character. No "0x"
 * prefix and no overflow check.
 *
 * @param[in] str Pointer to the null-terminated hexadecimal string.
 * @return The converted integer value.
 */
int atoh(const char *str);

/**
 * @brief Convert a signed integer to a string.
 *
 * @param[in]  value Integer value to convert.
 * @param[out] str   Pointer to the buffer where the resulting string will be stored.
 * @param[in]  base  Numerical base, 2 to 36; digits above 9 are lowercase.
 * @return Pointer to the resulting string. The caller MUST ensure the buffer is large enough.
 */
char *itoa(int value, char *str, unsigned int base);

/**
 * @brief Convert an unsigned integer to a string.
 *
 * @param[in]  value Unsigned integer value to convert.
 * @param[out] str   Pointer to the buffer where the resulting string will be stored.
 * @param[in]  base  Numerical base, 2 to 36; digits above 9 are lowercase.
 * @return Pointer to the resulting string. The caller MUST ensure the buffer is large enough.
 */
char *utoa(unsigned int value, char *str, unsigned int base);

/**
 * @brief Swaps the bytes of a 16-bit value (host to network order on this little-endian target).
 *
 * @param[in] x Value.
 * @return @p x byte-swapped.
 */
static inline uint16_t htons(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
/**
 * @brief Swaps the bytes of a 32-bit value (host to network order on this little-endian target).
 *
 * @param[in] x Value.
 * @return @p x byte-swapped.
 */
static inline uint32_t htonl(uint32_t x)
{
    return ((x >> 24)) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | (x << 24);
}

#define ntohs htons /**< Network to host order, 16-bit; the same swap as htons(). */
#define ntohl htonl /**< Network to host order, 32-bit; the same swap as htonl(). */

/**
 * @brief Reads a big-endian 32-bit value from unaligned memory.
 *
 * @param[in] p Four bytes, most significant first.
 * @return The value.
 */
static inline unsigned int be32(const void *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return ((unsigned int)b[0] << 24) | ((unsigned int)b[1] << 16) | ((unsigned int)b[2] << 8) |
           (unsigned int)b[3];
}

#ifdef __cplusplus
}
#endif

#endif