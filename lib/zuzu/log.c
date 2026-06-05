#include <stdarg.h>
#include <stdio.h>
#include <zuzu/log.h>
#include <zuzu/syspage.h>
#include "ansi.h"

static log_level_t g_min_level = LOG_LEVEL;

static const char *level_to_label(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_TRACE: return "TRACE";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_FATAL: return "FATAL";
        default: return "UNK";
    }
}

static const char *level_to_style(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_TRACE: return ANSI_BOLD ANSI_CYAN;
        case LOG_LEVEL_DEBUG: return ANSI_BOLD ANSI_GREEN;
        case LOG_LEVEL_INFO:  return ANSI_BOLD ANSI_BLUE;
        case LOG_LEVEL_WARN:  return ANSI_BOLD ANSI_YELLOW;
        case LOG_LEVEL_ERROR: return ANSI_BOLD ANSI_RED;
        case LOG_LEVEL_FATAL: return ANSI_BOLD ANSI_RED ANSI_BG_WHITE;
        default: return "";
    }
}

void log_set_level(log_level_t min_level) {
    g_min_level = min_level;
}

log_level_t log_get_level(void) {
    return g_min_level;
}

void log_write(log_level_t level, const char *tag, const char *fmt, ...) {
    if (level < g_min_level) return;
    if (!fmt) return;

    const char *lvl_label = level_to_label(level);
    const char *lvl_style = level_to_style(level);
    const char *safe_tag = tag ? tag : "";
    syspage_t *sp = (syspage_t *)SYSPAGE;
    unsigned long long ticks = (unsigned long long)sp->uptime_ticks;

    char msg_buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, ap);
    va_end(ap);

    char line_buf[256];
    if (safe_tag[0] != '\0') {
        snprintf(line_buf, sizeof(line_buf), "%s[%6llu %-5s]" ANSI_RESET " (%s) %s",
                 lvl_style, ticks, lvl_label, safe_tag, msg_buf);
    } else {
        snprintf(line_buf, sizeof(line_buf), "%s[%6llu %-5s]" ANSI_RESET " %s",
                 lvl_style, ticks, lvl_label, msg_buf);
    }

    printf("%s\n", line_buf);
}
