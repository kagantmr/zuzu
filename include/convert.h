#ifndef CONVERT_H
#define CONVERT_H

/**
 * @file convert.h
 * @brief Header file for string conversion functions.
 * This header declares functions for converting strings to integers and vice versa.
 */


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


static inline unsigned int be32(const void *p) {
    const unsigned char *b = (const unsigned char *)p;
    return ((unsigned int)b[0] << 24) | ((unsigned int)b[1] << 16) |
           ((unsigned int)b[2] << 8)  | (unsigned int)b[3];
}

#endif