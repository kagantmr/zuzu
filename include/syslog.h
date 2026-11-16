/**
 * @file
 * @brief POSIX system log, backed by logsvc through LogWrite().
 *
 * Do not include this together with <util/log.h>: both define LOG_DEBUG.
 */

#ifndef ZUZU_SYSLOG_H
#define ZUZU_SYSLOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief System is unusable. */
#define LOG_EMERG 0
/** @brief Act at once. */
#define LOG_ALERT 1
/** @brief Critical condition. */
#define LOG_CRIT 2
/** @brief Error. */
#define LOG_ERR 3
/** @brief Warning. */
#define LOG_WARNING 4
/** @brief Normal but significant. */
#define LOG_NOTICE 5
/** @brief Informational. */
#define LOG_INFO 6
/** @brief Debug detail. */
#define LOG_DEBUG 7

/** @brief Mask holding only priority @p pri. */
#define LOG_MASK(pri) (1 << (pri))
/** @brief Mask holding @p pri and every more important priority. */
#define LOG_UPTO(pri) ((1 << ((pri) + 1)) - 1)

/** @brief Mask of the priority bits in a syslog() argument. */
#define LOG_PRIMASK 0x07
/** @brief openlog() option: add the process id to each message. */
#define LOG_PID 0x01
/** @brief openlog() option: accepted and ignored. */
#define LOG_CONS 0x02
/** @brief openlog() option: accepted and ignored. */
#define LOG_NDELAY 0x08
/** @brief openlog() option: also write each message to stderr. */
#define LOG_PERROR 0x20

/** @brief Facility: accepted and ignored. */
#define LOG_USER (1 << 3)
/** @brief Facility: accepted and ignored. */
#define LOG_DAEMON (3 << 3)

/**
 * @brief Sets the tag and options for later syslog() calls.
 *
 * @param[in] ident    Tag put on each message; copied, and cut to 31 characters. NULL for none.
 * @param[in] option   LOG_PID, LOG_PERROR; the rest are ignored.
 * @param[in] facility Ignored.
 */
void openlog(const char *ident, int option, int facility);

/** @brief Forgets the tag and options set by openlog(). */
void closelog(void);

/**
 * @brief Sets which priorities syslog() lets through.
 *
 * @param[in] mask Priorities to keep, built with LOG_MASK() or LOG_UPTO(); 0 leaves it unchanged.
 * @return The previous mask.
 */
int setlogmask(int mask);

/**
 * @brief Logs a message.
 *
 * Priorities map to logsvc levels: EMERG, ALERT and CRIT to FATAL; ERR to ERROR; WARNING to WARN;
 * NOTICE and INFO to INFO; DEBUG to DEBUG.
 *
 * @param[in] priority LOG_EMERG to LOG_DEBUG, optionally with a facility, which is ignored.
 * @param[in] format   printf-style format.
 */
void syslog(int priority, const char *format, ...);

/**
 * @brief syslog() with a va_list.
 *
 * @param[in] priority As for syslog().
 * @param[in] format   printf-style format.
 * @param[in] ap       The arguments.
 */
void vsyslog(int priority, const char *format, va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_SYSLOG_H */
