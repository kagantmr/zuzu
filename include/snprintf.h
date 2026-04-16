#ifndef ZUZU_SNPRINTF_H
#define ZUZU_SNPRINTF_H

#include <stdarg.h>
#include <stddef.h>

/**
 * @brief Format a string into a buffer with size limit.
 * 
 * Follows C99 snprintf semantics:
 * - Writes at most (size - 1) characters plus null terminator
 * - Returns total characters that *would* have been written
 *   (excluding null), even if output was truncated
 * - If size is 0, nothing is written but return value is still
 *   the total length
 *
 * Built on top of vstrfmt's callback architecture.
 *
 * Note: supports nested calls on the same core using per-call context.
 * It is not intended as a cross-core synchronization primitive.
 */
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

#endif