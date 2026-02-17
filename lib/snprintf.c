#include <stdarg.h>
#include <stddef.h>
#include "lib/string.h"  // for vstrfmt

static struct {
    char *buf;
    size_t pos;
    size_t max;  // max chars to write (excluding null terminator)
} snprintf_ctx;

static void snprintf_outc(char c)
{
    if (snprintf_ctx.pos < snprintf_ctx.max) {
        snprintf_ctx.buf[snprintf_ctx.pos] = c;
    }
    // Always increment pos to track total length (like real snprintf)
    snprintf_ctx.pos++;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    if (!buf || size == 0) {
        // Still need to compute length
        snprintf_ctx.buf = NULL;
        snprintf_ctx.pos = 0;
        snprintf_ctx.max = 0;
        vstrfmt(snprintf_outc, fmt, args);
        return (int)snprintf_ctx.pos;
    }

    snprintf_ctx.buf = buf;
    snprintf_ctx.pos = 0;
    snprintf_ctx.max = size - 1;  // reserve space for null terminator

    vstrfmt(snprintf_outc, fmt, args);

    // Null terminate
    size_t term_pos = snprintf_ctx.pos < (size - 1) ? snprintf_ctx.pos : (size - 1);
    buf[term_pos] = '\0';

    // Return total chars that *would* have been written (excluding null)
    return (int)snprintf_ctx.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}