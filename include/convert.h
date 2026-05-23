#ifndef CONVERT_H
#define CONVERT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

 /**
  * @brief Convert a hexadecimal string to an integer.
  * @param str Pointer to the null-terminated hexadecimal string.
  * @return The converted integer value.
  */
int atoh(const char *str);

/**
 * @brief Convert a string to an integer.
 * 
 * @param str Pointer to the null-terminated string.
 * @return The converted integer value.
 */
int atoi(const char *str);

/**
 * @brief Convert a signed integer to a string.
 * 
 * @param value Integer value to convert.
 * @param str   Pointer to the buffer where the resulting string will be stored.
 * @param base  Numerical base for conversion (e.g., 10 for decimal, 16 for hexadecimal).
 * @return Pointer to the resulting string. The caller MUST ensure the buffer is large enough.
 */
char *itoa(int value, char *str, unsigned int base);

/**
 * @brief Convert an unsigned integer to a string.
 * 
 * @param value Unsigned integer value to convert.
 * @param str   Pointer to the buffer where the resulting string will be stored.
 * @param base  Numerical base for conversion (e.g., 10 for decimal, 16 for hexadecimal).
 * @return Pointer to the resulting string. The caller MUST ensure the buffer is large enough.
 */
char *utoa(unsigned int value, char *str, unsigned int base);

static inline uint16_t htons(uint16_t x) { return (x >> 8) | (x << 8); }
static inline uint32_t htonl(uint32_t x) { return ((x >> 24)) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | (x << 24); }

#define ntohs htons
#define ntohl htonl

static inline unsigned int be32(const void *p) {
    const unsigned char *b = (const unsigned char *)p;
    return ((unsigned int)b[0] << 24) | ((unsigned int)b[1] << 16) |
           ((unsigned int)b[2] << 8)  | (unsigned int)b[3];
}

#ifdef __cplusplus
}
#endif

#endif