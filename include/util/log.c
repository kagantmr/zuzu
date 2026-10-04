#include "ansi.h"
#include <stdarg.h>
#include <stdio.h>
#include <util/log.h>
#include <zuzu/syspage.h>

static LogLevel g_min_level = CONFIG_LOG_LEVEL;

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

    const char *lvl_label = LevelToLabel(level);
    const char *lvl_style = LevelToStyle(level);
    const char *safe_tag = tag ? tag : "";
    Syspage *sp = (Syspage *)SYSPAGE;
    unsigned long long ticks = (unsigned long long)sp->uptime_ticks;

    char msg_buf[192];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);
    va_end(ap);

    char line_buf[256];
    if (safe_tag[0] != '\0') {
        (void)snprintf(line_buf, sizeof(line_buf), "%s[%6llu %-5s]" ANSI_RESET " (%s) %s",
                       lvl_style, ticks, lvl_label, safe_tag, msg_buf);
    } else {
        (void)snprintf(line_buf, sizeof(line_buf), "%s[%6llu %-5s]" ANSI_RESET " %s", lvl_style,
                       ticks, lvl_label, msg_buf);
    }

    printf("%s\n", line_buf);
}
