/**
 * @file
 * @brief Userspace leveled logging: LOG_DEBUG()..LOG_ERROR() go through LogWrite() with a run-time
 * minimum level. The kernel uses <core/log.h> instead.
 */

#ifndef ZUZU_LOG_H
#define ZUZU_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Log severity, lowest first. */
typedef enum {
    LOG_LEVEL_TRACE = 0, /**< Very verbose tracing. */
    LOG_LEVEL_DEBUG = 1, /**< Debugging detail. */
    LOG_LEVEL_INFO = 2,  /**< Normal operation. */
    LOG_LEVEL_WARN = 3,  /**< Something unexpected but recoverable. */
    LOG_LEVEL_ERROR = 4, /**< An operation failed. */
    LOG_LEVEL_FATAL = 5, /**< The program cannot continue. */
} LogLevel;

/**
 * @brief Sets the lowest level LogWrite() prints.
 *
 * @param[in] min_level Messages below this level are dropped.
 */
void LogSetLevel(LogLevel min_level);

/**
 * @brief Returns the lowest level LogWrite() prints.
 *
 * @return The current minimum level.
 */
LogLevel LogGetLevel(void);

/**
 * @brief Prints one log line to stdout: level, uptime in ms, optional tag, message.
 *
 * Messages longer than 191 characters are cut.
 *
 * @param[in] level Severity; dropped if below the minimum level.
 * @param[in] tag   Short source name shown in parentheses, or NULL.
 * @param[in] fmt   printf-style format.
 */
void LogWrite(LogLevel level, const char *tag, const char *fmt, ...);

/** @brief Logs at DEBUG level under @p tag. */
#define LOG_DEBUG(tag, fmt, ...) LogWrite(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
/** @brief Logs at INFO level under @p tag. */
#define LOG_INFO(tag, fmt, ...) LogWrite(LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
/** @brief Logs at WARN level under @p tag. */
#define LOG_WARN(tag, fmt, ...) LogWrite(LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
/** @brief Logs at ERROR level under @p tag. */
#define LOG_ERROR(tag, fmt, ...) LogWrite(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)


#ifdef __cplusplus
}
#endif

#endif // ZUZU_LOG_H