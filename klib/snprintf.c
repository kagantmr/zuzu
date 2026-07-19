#include "string.h" 

typedef struct {
    char *buf;
    size_t pos;
    size_t max;  // max chars to write (excluding null terminator)
} snprintf_ctx_t;

static void snprintf_outc(void *ctx, char c)
{
    snprintf_ctx_t *s = (snprintf_ctx_t *)ctx;
    if (s->pos < s->max && s->buf) s->buf[s->pos] = c;
    s->pos++;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    va_list ap;
    va_copy(ap, args);

    snprintf_ctx_t local_ctx;
    local_ctx.buf = (buf && size) ? buf : NULL;
    local_ctx.pos = 0;
    local_ctx.max = (size > 0) ? (size - 1) : 0;

    vstrfmt(snprintf_outc, &local_ctx, fmt, &ap);
    va_end(ap);

    if (buf && size) {
        size_t term = (local_ctx.pos < (size - 1)) ? local_ctx.pos : (size - 1);
        buf[term] = '\0';
    }

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
