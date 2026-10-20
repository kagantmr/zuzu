/**
 * @file
 * @brief Freestanding stdlib subset: conversions, sorting, searching and rand().
 */

#ifndef STDLIB_H
#define STDLIB_H

#include <malloc.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Converts a decimal string to an integer.
 *
 * Skips leading whitespace, accepts a sign, and stops at the first non-digit. No overflow check.
 *
 * @param[in] str The null-terminated string.
 * @return The converted value.
 */
int atoi(const char *str);

/**
 * @brief Converts a string to a long integer.
 *
 * Accepts leading whitespace, a sign, and a 0x prefix in base 16. No overflow check.
 *
 * @param[in]  nptr   The string to convert.
 * @param[out] endptr Receives a pointer past the last digit used, or @p nptr if there were none;
 *                    may be NULL.
 * @param[in]  base   2 to 36, or 0 to pick 16 for "0x", 8 for a leading 0, else 10.
 * @return The converted value.
 */
long strtol(const char *nptr, char **endptr, int base);

/**
 * @brief Converts a string to an unsigned long integer.
 *
 * As strtol(); a minus sign is skipped and ignored.
 *
 * @param[in]  nptr   The string to convert.
 * @param[out] endptr As strtol(); may be NULL.
 * @param[in]  base   As strtol().
 * @return The converted value.
 */
unsigned long strtoul(const char *nptr, char **endptr, int base);

/**
 * @brief Converts a string to a double-precision floating-point number.
 *
 * Decimal only: digits, an optional fraction and an optional exponent. No inf, nan or hex.
 *
 * @param[in]  nptr   The string to convert.
 * @param[out] endptr Receives a pointer past the last character used; may be NULL.
 * @return The converted value.
 */
double strtod(const char *nptr, char **endptr);

/**
 * @brief Computes the absolute value of an integer.
 *
 * @param[in] j The integer value.
 * @return The absolute value of the integer.
 */
int abs(int j);

/**
 * @brief Uses quicksort to sort an array of elements.
 *
 * Not stable.
 *
 * @param[in,out] base   Pointer to the first element of the array to be sorted.
 * @param[in]     nmemb  Number of elements in the array.
 * @param[in]     size   Size of each element in bytes.
 * @param[in]     compar Returns less than, equal to or greater than zero as its first argument
 *                       sorts before, with or after its second.
 */
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

/**
 * @brief Performs a binary search on a sorted array.
 *
 * @param[in] key    Pointer to the key to search for.
 * @param[in] base   Pointer to the first element of the sorted array.
 * @param[in] nmemb  Number of elements in the array.
 * @param[in] size   Size of each element in bytes.
 * @param[in] compar Compares the key (first argument) with an element (second), as for qsort().
 * @return Pointer to the matching element in the array, or NULL if the key is not found.
 */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

/**
 * @brief Generates a pseudo-random integer with a linear congruential generator; not for
 * anything security-related.
 *
 * @return A value from 0 to 0x7FFFFFFF.
 */
int rand(void);

/**
 * @brief Seeds the pseudo-random number generator used by rand().
 *
 * @param[in] seed Seed; 0 is treated as 1.
 */
void srand(unsigned int seed);

#ifdef __cplusplus
}
#endif

#endif /* STDLIB_H */
