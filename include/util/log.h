/**
 * @file
 * @brief Leveled logging.
 *
 * In the kernel (`__ZUZU__` defined) the KTRACE()..KFATAL() macros print to
 * the kernel console and compile away below CONFIG_LOG_LEVEL. In userspace,
 * LOG_DEBUG()..LOG_ERROR() go through LogWrite() with a run-time minimum level.
 */

#ifndef ZUZU_LOG_H
#define ZUZU_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Level number for TRACE, usable in preprocessor comparisons. */
#define ZUZU_LOG_LEVEL_TRACE 0
/** @brief Level number for DEBUG. */
#define ZUZU_LOG_LEVEL_DEBUG 1
/** @brief Level number for INFO. */
#define ZUZU_LOG_LEVEL_INFO 2
/** @brief Level number for WARN. */
#define ZUZU_LOG_LEVEL_WARN 3
/** @brief Level number for ERROR. */
#define ZUZU_LOG_LEVEL_ERROR 4
/** @brief Level number for FATAL. */
#define ZUZU_LOG_LEVEL_FATAL 5

/** @brief Log severity, lowest first. */
typedef enum {
    LOG_LEVEL_TRACE = 0, /**< Very verbose tracing. */
    LOG_LEVEL_DEBUG = 1, /**< Debugging detail. */
    LOG_LEVEL_INFO = 2,  /**< Normal operation. */
    LOG_LEVEL_WARN = 3,  /**< Something unexpected but recoverable. */
    LOG_LEVEL_ERROR = 4, /**< An operation failed. */
    LOG_LEVEL_FATAL = 5, /**< The program cannot continue. */
} LogLevel;

#if !defined(__ZUZU__) || defined(__DOXYGEN__)

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

#endif

#ifdef __ZUZU__
#ifndef CONFIG_LOG_LEVEL
/** @brief Lowest level compiled into the kernel; normally set by Kconfig. */
#define CONFIG_LOG_LEVEL LOG_LEVEL_INFO
#endif

#include "ansi.h"
#include "core/kprintf.h"
#include "kernel/clock.h"

#ifndef LOG_FMT
/** @brief Per-file prefix hook: define before including this header to tag every message. */
#define LOG_FMT(fmt) fmt
#endif

#if CONFIG_LOG_LEVEL <= ZUZU_LOG_LEVEL_TRACE
/** @brief Kernel log line for very verbose tracing, with uptime in ms. */
#define KTRACE(fmt, ...)                                                                           \
    kprintf(ANSI_BOLD ANSI_CYAN "[%6llu TRACE] " ANSI_RESET LOG_FMT(fmt) "\n",                     \
            (unsigned long long)ClockNowMs(), ##__VA_ARGS__)
#else
#define KTRACE(fmt, ...)                                                                           \
    do {                                                                                           \
    } while (0)
#endif

#if CONFIG_LOG_LEVEL <= ZUZU_LOG_LEVEL_DEBUG
/** @brief Kernel log line for debugging detail, with uptime in ms. */
#define KDEBUG(fmt, ...)                                                                           \
    kprintf(ANSI_BOLD ANSI_GREEN "[%6llu DEBUG] " ANSI_RESET LOG_FMT(fmt) "\n",                    \
            (unsigned long long)ClockNowMs(), ##__VA_ARGS__)
#else
#define KDEBUG(fmt, ...)                                                                           \
    do {                                                                                           \
    } while (0)
#endif

#if CONFIG_LOG_LEVEL <= ZUZU_LOG_LEVEL_INFO
/** @brief Kernel log line for normal operation, with uptime in ms. */
#define KINFO(fmt, ...)                                                                            \
    kprintf(ANSI_BOLD ANSI_BLUE "[%6llu INFO ] " ANSI_RESET LOG_FMT(fmt) "\n",                     \
            (unsigned long long)ClockNowMs(), ##__VA_ARGS__)
#else
#define KINFO(fmt, ...)                                                                            \
    do {                                                                                           \
    } while (0)
#endif

#if CONFIG_LOG_LEVEL <= ZUZU_LOG_LEVEL_WARN
/** @brief Kernel log line for a recoverable problem, with uptime in ms. */
#define KWARN(fmt, ...)                                                                            \
    kprintf(ANSI_BOLD ANSI_YELLOW "[%6llu WARN ] " ANSI_RESET LOG_FMT(fmt) "\n",                   \
            (unsigned long long)ClockNowMs(), ##__VA_ARGS__)
#else
#define KWARN(fmt, ...)                                                                            \
    do {                                                                                           \
    } while (0)
#endif

#if CONFIG_LOG_LEVEL <= ZUZU_LOG_LEVEL_ERROR
/** @brief Kernel log line for a failed operation, with uptime in ms. */
#define KERROR(fmt, ...)                                                                           \
    kprintf(ANSI_BOLD ANSI_RED "[%6llu ERR  ] " ANSI_RESET LOG_FMT(fmt) "\n",                      \
            (unsigned long long)ClockNowMs(), ##__VA_ARGS__)
#else
#define KERROR(fmt, ...)                                                                           \
    do {                                                                                           \
    } while (0)
#endif

#if CONFIG_LOG_LEVEL <= ZUZU_LOG_LEVEL_FATAL
/** @brief Kernel log line for an unrecoverable problem, with uptime in ms. */
#define KFATAL(fmt, ...)                                                                           \
    kprintf(ANSI_BOLD ANSI_GRAY "[%6llu FATAL]" ANSI_RESET LOG_FMT(fmt) "\n",                      \
            (unsigned long long)ClockNowMs(), ##__VA_ARGS__)
#else
#define KFATAL(fmt, ...)                                                                           \
    do {                                                                                           \
    } while (0)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif // ZUZU_LOG_H