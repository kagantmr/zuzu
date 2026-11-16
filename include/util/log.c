/**
 * @file
 * @brief Userspace leveled logging.
 */

#include "ansi.h"
#include <dev/log.h>
#include <stdarg.h>
#include <stdio.h>
#include <util/log.h>
#include <zuzu/syspage.h>
#include <zuzu/uptime.h>

/** @brief Lowest level LogWrite() prints; set with LogSetLevel(). */
static LogLevel g_min_level = CONFIG_LOG_LEVEL;

/** @brief How long to wait before looking for logsvc again after it was not there. */
#define LOGSVC_RETRY_US 1000000U

/** @brief The session with logsvc, once there is one. */
static LogConn g_conn;
/** @brief @c g_conn is open. */
static bool g_connected;
/** @brief Earliest time to look for logsvc again. */
static uint64_t g_retry_us;
/** @brief LogWrite() is running: a nested call falls back to stdout. */
static bool g_busy;

/**
 * @brief Opens the session with logsvc if it is not open and was not tried too recently.
 * @param[in] tag The caller's first tag, used as the session's default.
 * @return true if there is a session.
 */
static bool LogsvcEnsure(const char *tag)
{
    if (g_connected)
        return true;
    uint64_t now = UptimeUs();
    if (now < g_retry_us)
        return false;
    g_retry_us = now + LOGSVC_RETRY_US;

    Handle port = LookupService("/svc/log");
    if (port < 0)
        return false;
    Err rc = LogClientConnect(port, tag, &g_conn);
    HandleClose(port); /* the session has its own handle */
    if (rc != ZUZU_OK)
        return false;
    g_connected = true;
    return true;
}

/**
 * @brief Sends one record to logsvc, reconnecting once if logsvc dropped the session.
 * @param[in] level Severity.
 * @param[in] tag   Source tag, or NULL.
 * @param[in] msg   Message text.
 * @param[in] len   Its length.
 * @return true if logsvc took the record or counted it as dropped; false if there is no logsvc.
 */
static bool LogsvcSend(LogLevel level, const char *tag, const char *msg, uint16_t len)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!LogsvcEnsure(tag))
            return false;
        Err rc = LogClientWrite(&g_conn, (uint8_t)level, tag, msg, len);
        if (rc != ERR_DEAD)
            return true; /* ERR_BUFFULL is counted in the pipe: the record is dropped by design */
        ClosePipe(&g_conn.w);
        HandleClose(g_conn.port);
        g_connected = false;
        g_retry_us = 0;
    }
    return false;
}

void LogShutdown(void)
{
    if (!g_connected)
        return;
    LogClientClose(&g_conn);
    g_connected = false;
}

/**
 * @brief Returns the fixed-width label printed for a level.
 *
 * @param[in] level Log level.
 * @return "TRACE".."FATAL", or "UNK".
 */
static const char *LevelToLabel(LogLevel level)
{
    switch (level) {
    case LOG_LEVEL_TRACE:
        return "TRACE";
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_FATAL:
        return "FATAL";
    default:
        return "UNK";
    }
}

/**
 * @brief Returns the ANSI colour sequence used for a level.
 *
 * @param[in] level Log level.
 * @return An escape sequence, or "" for unknown levels.
 */
static const char *LevelToStyle(LogLevel level)
{
    switch (level) {
    case LOG_LEVEL_TRACE:
        return ANSI_BOLD ANSI_CYAN;
    case LOG_LEVEL_DEBUG:
        return ANSI_BOLD ANSI_GREEN;
    case LOG_LEVEL_INFO:
        return ANSI_BOLD ANSI_BLUE;
    case LOG_LEVEL_WARN:
        return ANSI_BOLD ANSI_YELLOW;
    case LOG_LEVEL_ERROR:
        return ANSI_BOLD ANSI_RED;
    case LOG_LEVEL_FATAL:
        return ANSI_BOLD ANSI_RED ANSI_BG_WHITE;
    default:
        return "";
    }
}

void LogSetLevel(LogLevel min_level) { g_min_level = min_level; }

LogLevel LogGetLevel(void) { return g_min_level; }

void LogWrite(LogLevel level, const char *tag, const char *fmt, ...)
{
    if (level < g_min_level)
        return;
    if (!fmt)
        return;

    char text[256];
    va_list ap;
    va_start(ap, fmt);
    int tn = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (tn < 0)
        return;
    if ((size_t)tn >= sizeof(text))
        tn = (int)sizeof(text) - 1;
    if (!g_busy) {
        g_busy = true;
        bool sent = LogsvcSend(level, tag, text, (uint16_t)tn);
        g_busy = false;
        if (sent)
            return;
    }

    const char *lvl_label = LevelToLabel(level);
    const char *lvl_style = LevelToStyle(level);
    const char *safe_tag = tag ? tag : "";
    unsigned long long ticks = (unsigned long long)UptimeMs();

    char line_buf[256];
    if (safe_tag[0] != '\0') {
        (void)snprintf(line_buf, sizeof(line_buf), "%s[%6llu %-5s]" ANSI_RESET " (%s) %s",
                       lvl_style, ticks, lvl_label, safe_tag, text);
    } else {
        (void)snprintf(line_buf, sizeof(line_buf), "%s[%6llu %-5s]" ANSI_RESET " %s", lvl_style,
                       ticks, lvl_label, text);
    }

    printf("%s\n", line_buf);
}
