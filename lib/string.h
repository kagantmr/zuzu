#ifndef STRING_H
#define STRING_H

#include "stdarg.h"
#include "stddef.h"


/**
 * @brief Calculate the length of a null-terminated string.
 * 
 * @param s Pointer to the input string.
 * @return Number of characters in the string (excluding the null terminator).
 */
size_t strlen(const char *s);

/**
 * @brief Calculate the length of a string up to a maximum number of characters.
 * @param s Pointer to the input string.
 * @param maxlen Maximum number of characters to examine.
 * @return Number of characters in the string up to maxlen (excluding the null terminator
 */
size_t strnlen(const char *s, size_t maxlen);

/**
 * @brief Append one string to another.
 * 
 * @param dest Destination buffer (must be large enough to hold both strings).
 * @param src  Source string to append.
 * @return Pointer to dest.
 */
char *strcat(char *dest, const char *src);

/**
 * @brief Append at most n characters from one string to another.
 * 
 * @param dest Destination buffer.
 * @param src  Source string.
 * @param n    Maximum number of characters to append.
 * @return Pointer to dest.
 */
char *strncat(char *dest, const char *src, size_t n);

/**
 * @brief Copy a string from src to dest.
 * 
 * @param dest Destination buffer.
 * @param src  Source string.
 * @return Pointer to dest.
 */
char *strcpy(char *dest, const char *src);

/**
 * @brief Compare two strings lexicographically.
 * 
 * @param s1 First string.
 * @param s2 Second string.
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2.
 */
int strcmp(const char *s1, const char *s2);

/**
 * @brief Compare up to n characters of two strings.
 * 
 * @param s1 First string.
 * @param s2 Second string.
 * @param n  Maximum number of characters to compare.
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2.
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * @brief Copy up to n characters from src to dest.
 * 
 * @param dest Destination buffer.
 * @param src  Source string.
 * @param n    Maximum number of characters to copy.
 * @return Pointer to dest.
 */
char *strncpy(char *dest, const char *src, size_t n);

/**
 * @brief Locate the first occurrence of a character in a string.
 * @param s Pointer to the input string.
 * @param c Character to locate (as an int, but treated as char).
 * @return Pointer to the first occurrence of c in s, or NULL if not found.
 */
char *strchr(const char *s, int c);
/**
 * @brief Format a string according to a format specifier list.
 * 
 * Works similarly to sprintf but minimal; typically supports
 * %c, %s, %d, %x, etc.
 * 
 * @param outc   Function pointer to output a single character.
 * @param fstring Format string.
 * @param ...     Variable arguments matching the format specifiers.
 * @return Pointer to a static or provided buffer containing the formatted string.
 */
void strfmt(void (*outc)(char), const char *fstring, ...) ;

/**
 * @brief Format a string according to a format specifier list using va_list.
 * 
 * Works similarly to vprintf but minimal; typically supports
 * %c, %s, %d, %x, etc.
 * 
 * @param outc   Function pointer to output a single character.
 * @param fstring Format string.
 * @param args    va_list of arguments matching the format specifiers.
 */
void vstrfmt(void (*outc)(char), const char *fstring, va_list args);



#endif