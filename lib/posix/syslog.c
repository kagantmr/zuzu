/**
 * @file
 * @brief POSIX syslog() on top of LogWrite().
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <util/log.h>
#undef LOG_DEBUG
#undef LOG_INFO
#include <syslog.h>

/** @brief Longest ident openlog() keeps, with its NUL. */
#define IDENT_MAX 32
/** @brief Longest message syslog() formats. */
#define MSG_MAX 256

static char g_ident[IDENT_MAX];
static int g_option;
static int g_mask = 0xFF;

void openlog(const char *ident, int option, int facility)
{
    (void)facility;
    g_ident[0] = '\0';
    if (ident)
        strncat(g_ident, ident, IDENT_MAX - 1);
    g_option = option;
}

void closelog(void)
{
    g_ident[0] = '\0';
    g_option = 0;
}

int setlogmask(int mask)
{
    int old = g_mask;
    if (mask != 0)
        g_mask = mask;
    return old;
}

/**
 * @brief Maps a syslog priority onto a logsvc level.
 * @param[in] pri A priority, 0 to 7.
 * @return The level.
 */
static LogLevel LevelOf(int pri)
{
    switch (pri) {
    case LOG_EMERG:
    case LOG_ALERT:
    case LOG_CRIT:
        return LOG_LEVEL_FATAL;
    case LOG_ERR:
        return LOG_LEVEL_ERROR;
    case LOG_WARNING:
        return LOG_LEVEL_WARN;
    case LOG_NOTICE:
    case LOG_INFO:
        return LOG_LEVEL_INFO;
    default:
        return LOG_LEVEL_DEBUG;
    }
}

void vsyslog(int priority, const char *format, va_list ap)
{
    int pri = priority & LOG_PRIMASK;
    if (!(g_mask & LOG_MASK(pri)) || !format)
        return;

    char msg[MSG_MAX];
    int n = 0;
    if (g_option & LOG_PID)
        n = snprintf(msg, sizeof(msg), "[%d] ", (int)getpid());
    if (n < 0 || (size_t)n >= sizeof(msg))
        n = 0;
    (void)vsnprintf(msg + n, sizeof(msg) - (size_t)n, format, ap);

    if (g_option & LOG_PERROR)
        fprintf(stderr, "%s%s%s\n", g_ident, g_ident[0] ? ": " : "", msg);
    LogWrite(LevelOf(pri), g_ident[0] ? g_ident : NULL, "%s", msg);
}

void syslog(int priority, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
}
