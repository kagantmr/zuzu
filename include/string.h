/**
 * @file
 * @brief Memory and string functions, and the formatting core.
 */

#ifndef STRING_H
#define STRING_H

#include <stdarg.h>
#include <stddef.h>

/** @brief Character sink for vstrfmt(): receives @p ctx and each output character. */
typedef void (*strfmt_outc_t)(void *ctx, char c);

/** @brief Rounds @p addr down to a multiple of @p align, a power of two. */
#define align_down(addr, align) ((addr) & ~((align) - 1u))
/** @brief Rounds @p addr up to a multiple of @p align, a power of two. */
#define align_up(addr, align) (((addr) + (align) - 1) & ~((align) - 1u))

/**
 * @brief Copy n bytes from source to destination.
 *
 * @param[out] dest Pointer to the destination memory area.
 * @param[in]  src  Pointer to the source memory area; must not overlap @p dest.
 * @param[in]  n    Number of bytes to copy.
 * @return Pointer to the destination memory area.
 */
void *memcpy(void *dest, const void *src, size_t n);

/**
 * @brief Set n bytes of memory to a specified value.
 *
 * @param[out] dst Pointer to the memory area to set.
 * @param[in]  c   Value to set (interpreted as an unsigned char).
 * @param[in]  n   Number of bytes to set.
 * @return Pointer to the memory area.
 */
void *memset(void *dst, int c, size_t n);

/**
 * @brief Move n bytes from source to destination, handling overlapping regions.
 *
 * @param[out] dest Pointer to the destination memory area.
 * @param[in]  src  Pointer to the source memory area.
 * @param[in]  n    Number of bytes to move.
 * @return Pointer to the destination memory area.
 */
void *memmove(void *dest, const void *src, size_t n);

/**
 * @brief Compare two memory areas byte by byte.
 *
 * @param[in] str1  Pointer to the first memory area.
 * @param[in] str2  Pointer to the second memory area.
 * @param[in] count Number of bytes to compare.
 * @return An integer less than, equal to, or greater than zero if str1 is found, respectively, to
 * be less than, to match, or be greater than str2.
 */
int memcmp(const void *str1, const void *str2, size_t count);

/**
 * @brief Scan a memory area for the first occurrence of a specified byte.
 *
 * @param[in] s Pointer to the memory area to scan.
 * @param[in] c Byte value to search for (interpreted as an unsigned char).
 * @param[in] n Number of bytes to scan.
 * @return Pointer to the byte, or NULL if it does not occur in the first @p n bytes.
 */
void *memchr(const void *s, int c, size_t n);

/**
 * @brief Calculate the length of a null-terminated string.
 *
 * @param[in] s Pointer to the input string.
 * @return Number of characters in the string (excluding the null terminator).
 */
size_t strlen(const char *s);

/**
 * @brief Calculate the length of a string up to a maximum number of characters.
 *
 * @param[in] s      Pointer to the input string.
 * @param[in] maxlen Maximum number of characters to examine.
 * @return Number of characters in the string, at most @p maxlen (excluding the null terminator).
 */
size_t strnlen(const char *s, size_t maxlen);

/**
 * @brief Copy a string from src to dest.
 *
 * @param[out] dest Destination buffer, large enough for @p src and its NUL.
 * @param[in]  src  Source string.
 * @return Pointer to dest.
 */
char *strcpy(char *dest, const char *src);

/**
 * @brief Copy at most @p n characters of a string, padding with NULs up to @p n.
 *
 * The result is not NUL-terminated if @p src has @p n or more characters.
 *
 * @param[out] dest Destination buffer of at least @p n bytes.
 * @param[in]  src  Source string.
 * @param[in]  n    Bytes to write.
 * @return Pointer to dest.
 */
char *strncpy(char *dest, const char *src, size_t n);

char *strcat(char *dest, const char *src);

char *strncat(char *dest, const char *src, size_t n);

/**
 * @brief Compare two strings lexicographically.
 *
 * @param[in] s1 First string.
 * @param[in] s2 Second string.
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2.
 */
int strcmp(const char *s1, const char *s2);

/**
 * @brief Compare up to n characters of two strings.
 *
 * @param[in] s1 First string.
 * @param[in] s2 Second string.
 * @param[in] n  Maximum number of characters to compare.
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2.
 */
int strncmp(const char *s1, const char *s2, size_t n);

/**
 * @brief Locate the first occurrence of a character in a string.
 *
 * @param[in] s Pointer to the input string.
 * @param[in] c Character to locate (as an int, but treated as char).
 * @return Pointer to the first occurrence of c in s, or NULL if not found.
 */
char *strchr(const char *s, int c);

/**
 * @brief Locate the last occurrence of a character in a string.
 *
 * @param[in] s Pointer to the input string.
 * @param[in] c Character to locate (as an int, but treated as char).
 * @return Pointer to the last occurrence of c in s, or NULL if not found.
 */
char *strrchr(const char *s, int c);

/**
 * @brief Format a string according to a format specifier list using va_list.
 *
 * Supports format specifiers:
 *   %c, %s, %d, %i, %u, %x, %X, %o, %b, %p, %P, %%
 *
 * Length modifiers: hh, h, l, ll, z (for size_t/ptrdiff_t)
 * Flags: -, 0, +, space, #
 * Width and precision are supported.
 *
 * Examples: %zu (unsigned size_t), %zd (signed size_t), %zx (size_t in hex)
 *
 * @param[in]     outc    Function pointer to output a single character.
 * @param[in]     ctx     Context pointer passed to outc.
 * @param[in]     fstring Format string.
 * @param[in,out] args    Pointer to va_list of arguments matching the format specifiers.
 */
void vstrfmt(strfmt_outc_t outc, void *ctx, const char *fstring, va_list *args);

/** @brief Bounded formatted output into a buffer; documented in snprintf.h. */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

/** @brief Bounded formatted output into a buffer; documented in snprintf.h. */
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif
