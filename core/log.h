/**
 * @file log.h
 * @brief Kernel leveled logging: the KTRACE()..KFATAL() macros print to the kernel console and
 * compile away below CONFIG_LOG_LEVEL. Userspace uses <util/log.h> instead.
 */

#ifndef CORE_LOG_H
#define CORE_LOG_H

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

#ifndef CONFIG_LOG_LEVEL
/** @brief Lowest level compiled into the kernel; normally set by Kconfig. */
#define CONFIG_LOG_LEVEL ZUZU_LOG_LEVEL_INFO
#endif

#include <ansi.h>
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

#endif // CORE_LOG_H
