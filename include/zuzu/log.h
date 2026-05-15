#ifndef ZUZU_LOG_H
#define ZUZU_LOG_H

#define ZUZU_LOG_LEVEL_TRACE 0
#define ZUZU_LOG_LEVEL_DEBUG 1
#define ZUZU_LOG_LEVEL_INFO  2
#define ZUZU_LOG_LEVEL_WARN  3
#define ZUZU_LOG_LEVEL_ERROR 4
#define ZUZU_LOG_LEVEL_FATAL 5

typedef enum {
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_WARN = 3,
    LOG_LEVEL_ERROR = 4,
    LOG_LEVEL_FATAL = 5,
} log_level_t;

void log_set_level(log_level_t min_level);
log_level_t log_get_level(void);
void log_write(log_level_t level, const char *tag, const char *fmt, ...);

#define LOG_DEBUG(tag, fmt, ...) log_write(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  log_write(LOG_LEVEL_INFO,  tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  log_write(LOG_LEVEL_WARN,  tag, fmt, ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) log_write(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#ifdef __KERNEL__
#include "core/kprintf.h"
#include "kernel/time/tick.h"
#include "ansi.h"

#ifndef LOG_FMT
#define LOG_FMT(fmt) fmt
#endif

#if LOG_LEVEL <= ZUZU_LOG_LEVEL_TRACE
#define KTRACE(fmt, ...) kprintf(ANSI_BOLD ANSI_CYAN "[%6llu TRACE] " ANSI_RESET LOG_FMT(fmt) "\n", (unsigned long long)get_ticks(), ##__VA_ARGS__)
#else
#define KTRACE(fmt, ...) do {} while (0)
#endif

#if LOG_LEVEL <= ZUZU_LOG_LEVEL_DEBUG
#define KDEBUG(fmt, ...) kprintf(ANSI_BOLD ANSI_GREEN "[%6llu DEBUG] " ANSI_RESET LOG_FMT(fmt) "\n", (unsigned long long)get_ticks(), ##__VA_ARGS__)
#else
#define KDEBUG(fmt, ...) do {} while (0)
#endif

#if LOG_LEVEL <= ZUZU_LOG_LEVEL_INFO
#define KINFO(fmt, ...)  kprintf(ANSI_BOLD ANSI_BLUE "[%6llu INFO ] " ANSI_RESET LOG_FMT(fmt) "\n", (unsigned long long)get_ticks(), ##__VA_ARGS__)
#else
#define KINFO(fmt, ...) do {} while (0)
#endif

#if LOG_LEVEL <= ZUZU_LOG_LEVEL_WARN
#define KWARN(fmt, ...)  kprintf(ANSI_BOLD ANSI_YELLOW "[%6llu WARN ] " ANSI_RESET LOG_FMT(fmt) "\n", (unsigned long long)get_ticks(), ##__VA_ARGS__)
#else
#define KWARN(fmt, ...) do {} while (0)
#endif

#if LOG_LEVEL <= ZUZU_LOG_LEVEL_ERROR
#define KERROR(fmt, ...) kprintf(ANSI_BOLD ANSI_RED "[%6llu ERR  ] " ANSI_RESET LOG_FMT(fmt) "\n", (unsigned long long)get_ticks(), ##__VA_ARGS__)
#else
#define KERROR(fmt, ...) do {} while (0)
#endif

#if LOG_LEVEL <= ZUZU_LOG_LEVEL_FATAL
#define KFATAL(fmt, ...) kprintf(ANSI_BOLD ANSI_GRAY "[%6llu FATAL]" ANSI_RESET LOG_FMT(fmt) "\n", (unsigned long long)get_ticks(), ##__VA_ARGS__)
#else
#define KFATAL(fmt, ...) do {} while (0)
#endif
#endif

#endif // ZUZU_LOG_H