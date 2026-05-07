#include <zuzu/log.h>
#include <stdarg.h>

static log_level_t g_min_level = LOG_LEVEL_INFO;

void log_set_level(log_level_t min_level) {
    g_min_level = min_level;
}

void log_write(log_level_t level, const char *tag, const char *fmt, ...) {
    if (level < g_min_level) return;
    (void)tag; (void)fmt;
    /* Minimal implementation: discard messages for now. */
    va_list ap; va_start(ap, fmt); va_end(ap);
}
