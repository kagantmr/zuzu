#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>
#include <malloc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Converts a string to an integer.
 * 
 * @param str The string to convert.
 * @return int The converted integer value.
 */
int atoi(const char *str);

/**
 * @brief Converts a string to a long integer.
 *
 * @param nptr The string to convert.
 * @param endptr A pointer to a pointer to character. If not NULL, it will point to the character after the last character used in the conversion.
 * @param base The base for the conversion (e.g., 10 for decimal, 16 for hexadecimal).
 * @return long The converted long integer value.
 */
long strtol(const char *nptr, char **endptr, int base);

/**
 * @brief Converts a string to an unsigned long integer.
 * 
 * @param nptr The string to convert.
 * @param endptr A pointer to a pointer to character. If not NULL, it will point to the character after the last character used in the conversion.
 * @param base The base for the conversion (e.g., 10 for decimal, 16 for hexadecimal).
 * @return unsigned long The converted unsigned long integer value.
 */
unsigned long strtoul(const char *nptr, char **endptr, int base);

/**
 * @brief Converts a string to a double-precision floating-point number.
 * 
 * @param nptr The string to convert.
 * @param endptr A pointer to a pointer to character. If not NULL, it will point to the character after the last character used in the conversion.
 * @return double The converted double-precision floating-point value.
 */
double strtod(const char *nptr, char **endptr);

/**
 * @brief Computes the absolute value of an integer.
 * 
 * @param j The integer value.
 * @return int The absolute value of the integer.
 */
int abs(int j);

/**
 * @brief Uses quicksort to sort an array of elements.
 * 
 * @param base Pointer to the first element of the array to be sorted.
 * @param nmemb Number of elements in the array.
 * @param size Size of each element in bytes.
 * @param compar Comparison function that takes two const void* arguments and returns an integer less than, equal to, or greater than zero if the first argument is considered to be respectively less than, equal to, or greater than the second.
 */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));

/**
 * @brief Performs a binary search on a sorted array.
 * 
 * @param key Pointer to the key to search for.
 * @param base Pointer to the first element of the sorted array.
 * @param nmemb Number of elements in the array.
 * @param size Size of each element in bytes.
 * @param compar Comparison function that takes two const void* arguments and returns an integer less than, equal to, or greater than zero if the first argument is considered to be respectively less than, equal to, or greater than the second.
 * @return void* Pointer to the matching element in the array, or NULL if the key is not found.
 */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

/**
 * @brief Generates a pseudo-random integer.
 */
int rand(void);

/**
 * @brief Seeds the pseudo-random number generator used by rand().
 */
void srand(unsigned int seed);

#ifdef __cplusplus
}
#endif

#endif /* STDLIB_H */

