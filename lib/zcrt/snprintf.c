#include "string.h" 

typedef struct {
    char *buf;
    size_t pos;
    size_t max;  // max chars to write (excluding null terminator)
} snprintf_ctx_t;

static snprintf_ctx_t *g_snprintf_ctx;

static void snprintf_outc(char c)
{
    if (!g_snprintf_ctx)
        return;
    if (g_snprintf_ctx->pos < g_snprintf_ctx->max && g_snprintf_ctx->buf) {
        g_snprintf_ctx->buf[g_snprintf_ctx->pos] = c;
    }
    // Always increment pos to track total length (like real snprintf)
    g_snprintf_ctx->pos++;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    va_list ap;
    va_copy(ap, args);

    snprintf_ctx_t local_ctx;
    snprintf_ctx_t *prev_ctx = g_snprintf_ctx;
    g_snprintf_ctx = &local_ctx;

    if (!buf || size == 0) {
        // Still need to compute length
        local_ctx.buf = NULL;
        local_ctx.pos = 0;
        local_ctx.max = 0;
        vstrfmt(snprintf_outc, fmt, &ap);
        int ret = (int)local_ctx.pos;
        g_snprintf_ctx = prev_ctx;
        va_end(ap);
        return ret;
    }

    local_ctx.buf = buf;
    local_ctx.pos = 0;
    local_ctx.max = size - 1;  // reserve space for null terminator

    vstrfmt(snprintf_outc, fmt, &ap);
    va_end(ap);

    // Null terminate
    size_t term_pos = local_ctx.pos < (size - 1) ? local_ctx.pos : (size - 1);
    buf[term_pos] = '\0';

    // Return total chars that would have been written (excluding null)
    g_snprintf_ctx = prev_ctx;
    return (int)local_ctx.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}
