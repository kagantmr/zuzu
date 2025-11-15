#ifndef CONVERT_H
#define CONVERT_H
#include "stddef.h"

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




#endif