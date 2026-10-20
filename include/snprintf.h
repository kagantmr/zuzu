/**
 * @file
 * @brief Bounded formatted output.
 */

#ifndef ZUZU_SNPRINTF_H
#define ZUZU_SNPRINTF_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Formats into a buffer, C99-style.
 *
 * Writes at most @p size - 1 characters and a NUL. With @p size 0 nothing is
 * written. Safe to nest: each call keeps its own state.
 *
 * @param[out] buf  Output buffer.
 * @param[in]  size Size of @p buf.
 * @param[in]  fmt  printf format.
 * @param[in]  ...  Arguments.
 * @return The length the full output would have, excluding the NUL, even when truncated.
 */
int snprintf(char *buf, size_t size, const char *fmt, ...);

/**
 * @brief snprintf() taking a va_list.
 *
 * @param[out] buf  Output buffer.
 * @param[in]  size Size of @p buf.
 * @param[in]  fmt  printf format.
 * @param[in]  args Arguments.
 * @return As snprintf().
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

#ifdef __cplusplus
}
#endif

#endif