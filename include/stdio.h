/**
 * @file
 * @brief Console I/O over ttysvc: printf and scanf families, and choosing the terminal.
 */

#ifndef STDIO_H
#define STDIO_H

#include "snprintf.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifndef EOF
#define EOF (-1) /**< Returned by input functions at end of input or on error. */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Writes formatted output to the standard output stream (stdout).
 *
 * @param[in] format printf format.
 * @param[in] ... Arguments.
 * @return The number of characters printed (excluding the null byte used to end output to
 * strings), or a negative value if an error occurs.
 */
int printf(const char *format, ...);

/**
 * @brief Writes formatted output to the standard output stream (stdout) using a va_list.
 *
 * @param[in] format printf format.
 * @param[in] args Arguments.
 * @return The number of characters printed (excluding the null byte used to end output to
 * strings), or a negative value if an error occurs.
 */
int vprintf(const char *format, va_list args);

/**
 * @brief Writes formatted output to a string.
 *
 * @param[out] buf Output buffer; must be large enough, as nothing limits the length.
 * @param[in] format printf format.
 * @param[in] ... Arguments.
 * @return The number of characters written to the buffer (excluding the null byte used to end
 * output to strings), or a negative value if an error occurs.
 */
int sprintf(char *buf, const char *format, ...);

/**
 * @brief Writes formatted output to a string using a va_list.
 *
 * @param[out] buf Output buffer; must be large enough, as nothing limits the length.
 * @param[in] format printf format.
 * @param[in] args Arguments.
 * @return The number of characters written to the buffer (excluding the null byte used to end
 * output to strings), or a negative value if an error occurs.
 */
int vsprintf(char *buf, const char *format, va_list args);

/**
 * @brief Reads a character from the standard input stream (stdin).
 *
 * @return The character read as an unsigned char cast to an int or EOF on end of file or error.
 */
int getchar(void);

/**
 * @brief Reads one line from the console and parses it, like sscanf().
 *
 * @param[in] format scanf format.
 * @param[out] ... Where to store the converted items.
 * @return The number of input items successfully matched and assigned, or EOF if an input
 * failure occurs before any items are matched.
 */
int scanf(const char *format, ...);

/**
 * @brief scanf() taking a va_list.
 *
 * @param[in] format scanf format.
 * @param[out] args Where to store the converted items.
 * @return The number of input items successfully matched and assigned, or EOF if an input
 * failure occurs before any items are matched.
 */
int vscanf(const char *format, va_list args);

/**
 * @brief Connects stdio to ttysvc now, rather than on first use.
 *
 * @return 0 on success, or -1.
 */
int stdio_open_tty(void);

/**
 * @brief Switches the console between cooked (line editing and echo by the
 * terminal service, the default) and raw (every byte passed through; the
 * caller echoes). In raw mode printf still turns "\n" into "\r\n".
 *
 * The setting survives a reconnect.
 *
 * @param[in] enable Non-zero for raw, 0 for cooked.
 * @return 0 on success, or -1.
 */
int stdio_set_raw(int enable);

/**
 * @brief Detaches from the tty so another program can take the foreground.
 * The next stdio call attaches again.
 */
void stdio_close_tty(void);

/**
 * @brief Routes the standard I/O functions to a specific TTY device.
 *
 * Reconnects at once, so a bad alias is reported here rather than at the next printf.
 *
 * @param[in] name A ttysvc alias (e.g. "tty0"), or "" for the default endpoint.
 * @return 0 on success, or -1.
 */
int stdio_route_tty(const char *name);

/**
 * @brief stdio_route_tty() to "tty<index>".
 *
 * @param[in] index Terminal number; only its last decimal digit is used.
 * @return As stdio_route_tty().
 */
int stdio_use_tty(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif
