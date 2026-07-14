#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "snprintf.h"

#ifndef EOF
#define EOF (-1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Writes formatted output to the standard output stream (stdout).
 * 
 * @param format A C string that contains a format string that follows the same specifications as printf.
 * @param ... Additional arguments to be formatted and printed according to the format string.
 * @return int The number of characters printed (excluding the null byte used to end output to strings), or a negative value if an error occurs.
 */
int printf(const char *format, ...);

/**
 * @brief Writes formatted output to the standard output stream (stdout) using a va_list.
 * 
 * @param format A C string that contains a format string that follows the same specifications as printf.
 * @param args A va_list of arguments to be formatted and printed according to the format string.
 * @return int The number of characters printed (excluding the null byte used to end output to strings), or a negative value if an error occurs.
 */
int vprintf(const char *format, va_list args);

/**
 * @brief Writes formatted output to a string.
 * 
 * @param buf A pointer to the buffer where the formatted string will be stored.
 * @param format A C string that contains a format string that follows the same specifications as printf
 * @param ... Additional arguments to be formatted and stored in the buffer according to the format string.
 * @return int The number of characters written to the buffer (excluding the null byte used to end output to strings), or a negative value if an error occurs.
 */
int sprintf(char *buf, const char *format, ...);

/**
 * @brief Writes formatted output to a string using a va_list.
 * 
 * @param buf A pointer to the buffer where the formatted string will be stored.
 * @param format A C string that contains a format string that follows the same specifications as printf
 * @param args A va_list of arguments to be formatted and stored in the buffer according to the format string.
 * @return int The number of characters written to the buffer (excluding the null byte used to end output to strings), or a negative value if an error occurs.
 */
int vsprintf(char *buf, const char *format, va_list args);

/**
 * @brief Reads a character from the standard input stream (stdin).
 * 
 * @return int The character read as an unsigned char cast to an int or EOF on end of file or error.
 */
int getchar(void);

/**
 * @brief Reads formatted input from the standard input stream (stdin).
 * 
 * @param format A C string that contains a format string that follows the same specifications as scanf.
 * @param ... Additional arguments where the input will be stored according to the format string.
 * @return int The number of input items successfully matched and assigned, or EOF if an input failure occurs before any items are matched.
 */
int scanf(const char *format, ...);

/**
 * @brief Reads formatted input from the standard input stream (stdin) using a va_list.
 * 
 * @param format A C string that contains a format string that follows the same specifications as scanf.
 * @param args A va_list of arguments where the input will be stored according to the format string.
 * @return int The number of input items successfully matched and assigned, or EOF if an input failure occurs before any items are matched.
 */
int vscanf(const char *format, va_list args);

/**
 * @brief Registers a UART device for use with the standard I/O functions.
 * 
 * @return int Returns 0 on success, or a negative value if an error occurs.
 */
int stdio_register_uart(void);

/**
 * @brief Routes the standard I/O functions to a specific TTY device.
 * 
 * @param name A 4-character string representing the name of the TTY device (e.g., "tty0").
 * @return int Returns 0 on success, or a negative value if an error occurs.
 */
int stdio_route_tty(const char name[4]);

/**
 * @brief Sets the standard I/O functions to use a specific TTY device by index.
 */
int stdio_use_tty(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif
