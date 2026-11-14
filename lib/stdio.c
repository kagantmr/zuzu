#include <ctype.h>
#include <dev/tty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/stdio_core.h>
#include <zuzu/msg.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define STDIO_PRINTF_BUF_SIZE 1024
static int stdio_pushback = EOF;

static void __attribute__((destructor)) stdio_fini(void) { StdioFini(); }

/* Idempotent. Connects on first use rather than at startup so services that
 * only ever log never open a session. */
int stdio_open_tty(void) { return StdioOpenTty(); }

void stdio_on_interrupt(void (*handler)(void)) { StdioOnInterrupt(handler); }

/* Raw mode hands every byte through untouched, so the caller does its own
 * echo and editing. */
int stdio_set_raw(int enable) { return StdioSetRaw(enable); }

/* Releases the session so another program can take the foreground; the next
 * printf or getchar attaches again. */
void stdio_close_tty(void)
{
    StdioCloseTty();
    stdio_pushback = EOF;
}

/* `name` is a ttysvc alias ("" for the default endpoint). Reconnects now so
 * a bad name is reported here. */
int stdio_route_tty(const char *name)
{
    stdio_pushback = EOF;
    return StdioRouteTty(name);
}

int stdio_use_tty(uint32_t index)
{
    char name[TTY_NAME_MAX];
    snprintf(name, sizeof(name), "tty%u", (unsigned)(index % 10u));
    return stdio_route_tty(name);
}

/* Blocks while the pipe is full: on a terminal that is the backpressure ttysvc applies
 * to a program writing faster than the display drains. */
static void stdio_write(const char *s, size_t n) { StdioWrite(1, s, n); }

/* Blocks until a byte arrives. Reads in bursts: a terminal hands over a whole line at once. */
static int stdio_stream_getc(void)
{
    if (stdio_pushback != EOF) {
        int c = stdio_pushback;
        stdio_pushback = EOF;
        return c;
    }

    static uint8_t buf[128];
    static int pos, have;
    if (pos == have) {
        int n = StdioRead(buf, sizeof(buf));
        if (n <= 0)
            return EOF;
        pos = 0;
        have = n;
    }
    return buf[pos++];
}

int getchar(void) { return stdio_stream_getc(); }

static const char *stdio_skip_ws(const char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

static int stdio_vsscanf_line(const char *input, const char *format, va_list args)
{
    const char *src = input;
    const char *fmt = format;
    int assigned = 0;

    while (*fmt) {
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*fmt))
                fmt++;
            src = stdio_skip_ws(src);
            continue;
        }

        if (*fmt != '%') {
            if (*src != *fmt)
                break;
            src++;
            fmt++;
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            if (*src != '%')
                break;
            src++;
            fmt++;
            continue;
        }

        int suppress = 0;
        if (*fmt == '*') {
            suppress = 1;
            fmt++;
        }

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        enum {
            LEN_NONE,
            LEN_HH,
            LEN_H,
            LEN_L,
            LEN_LL,
            LEN_Z,
            LEN_T,
        } len = LEN_NONE;

        if (*fmt == 'h') {
            fmt++;
            len = LEN_H;
            if (*fmt == 'h') {
                fmt++;
                len = LEN_HH;
            }
        } else if (*fmt == 'l') {
            fmt++;
            len = LEN_L;
            if (*fmt == 'l') {
                fmt++;
                len = LEN_LL;
            }
        } else if (*fmt == 'z') {
            fmt++;
            len = LEN_Z;
        } else if (*fmt == 't') {
            fmt++;
            len = LEN_T;
        }

        char conv = *fmt++;
        if (conv != 'c' && conv != '[' && conv != 'n')
            src = stdio_skip_ws(src);

        if (!*src && conv != 'n')
            break;

        if (conv == 'c') {
            int count = width > 0 ? width : 1;
            if (!suppress) {
                char *out = va_arg(args, char *);
                for (int i = 0; i < count; i++) {
                    if (!*src)
                        return assigned;
                    out[i] = *src++;
                }
                assigned++;
            } else {
                for (int i = 0; i < count; i++) {
                    if (!*src)
                        return assigned;
                    src++;
                }
            }
            continue;
        }

        if (conv == 's') {
            char tmp[256];
            size_t used = 0;
            while (*src && !isspace((unsigned char)*src)) {
                if (width > 0 && used >= (size_t)width)
                    break;
                if (used + 1 < sizeof(tmp))
                    tmp[used++] = *src;
                src++;
            }
            if (used == 0)
                return assigned;
            tmp[used] = '\0';
            if (!suppress) {
                char *out = va_arg(args, char *);
                memcpy(out, tmp, used + 1);
                assigned++;
            }
            continue;
        }

        if (conv == 'n') {
            if (!suppress) {
                int *out = va_arg(args, int *);
                *out = (int)(src - input);
                assigned++;
            }
            continue;
        }

        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' || conv == 'x' ||
            conv == 'p' || conv == 'f' || conv == 'e' || conv == 'g' || conv == 'a') {
            char *end = NULL;
            long signed_value = 0;
            unsigned long unsigned_value = 0;

            switch (conv) {
            case 'd':
                signed_value = strtol(src, &end, 10);
                break;
            case 'i':
                signed_value = strtol(src, &end, 0);
                break;
            case 'u':
                unsigned_value = strtoul(src, &end, 10);
                break;
            case 'o':
                unsigned_value = strtoul(src, &end, 8);
                break;
            case 'x':
                unsigned_value = strtoul(src, &end, 16);
                break;
            case 'p':
                unsigned_value = strtoul(src, &end, 0);
                break;
            case 'f':
            case 'e':
            case 'g':
            case 'a': {
                double value = strtod(src, &end);
                if (!end || end == src)
                    return assigned;
                if (!suppress) {
                    if (len == LEN_L || len == LEN_LL) {
                        double *out = va_arg(args, double *);
                        *out = value;
                    } else {
                        float *out = va_arg(args, float *);
                        *out = (float)value;
                    }
                    assigned++;
                }
                src = end;
                continue;
            }
            }

            if (!end || end == src)
                return assigned;

            if (!suppress) {
                switch (conv) {
                case 'd':
                case 'i':
                    if (len == LEN_L)
                        *va_arg(args, long *) = signed_value;
                    else if (len == LEN_LL)
                        *va_arg(args, long long *) = (long long)signed_value;
                    else if (len == LEN_H)
                        *va_arg(args, short *) = (short)signed_value;
                    else if (len == LEN_HH)
                        *va_arg(args, signed char *) = (signed char)signed_value;
                    else if (len == LEN_Z)
                        *va_arg(args, size_t *) = (size_t)signed_value;
                    else if (len == LEN_T)
                        *va_arg(args, ptrdiff_t *) = (ptrdiff_t)signed_value;
                    else
                        *va_arg(args, int *) = (int)signed_value;
                    break;
                case 'u':
                case 'o':
                case 'x':
                    if (len == LEN_L)
                        *va_arg(args, unsigned long *) = unsigned_value;
                    else if (len == LEN_LL)
                        *va_arg(args, unsigned long long *) = (unsigned long long)unsigned_value;
                    else if (len == LEN_H)
                        *va_arg(args, unsigned short *) = (unsigned short)unsigned_value;
                    else if (len == LEN_HH)
                        *va_arg(args, unsigned char *) = (unsigned char)unsigned_value;
                    else if (len == LEN_Z)
                        *va_arg(args, size_t *) = (size_t)unsigned_value;
                    else
                        *va_arg(args, unsigned int *) = (unsigned int)unsigned_value;
                    break;
                case 'p':
                    *va_arg(args, void **) = (void *)(uintptr_t)unsigned_value;
                    break;
                }
                assigned++;
            }

            src = end;
            continue;
        }

        break;
    }

    return assigned;
}

static int stdio_read_line(char *dst, size_t max)
{
    if (!dst || max == 0)
        return EOF;

    size_t len = 0;
    int c;
    while ((c = stdio_stream_getc()) != EOF && c != '\n') {
        if (len + 1 < max)
            dst[len++] = (char)c;
    }

    if (c == EOF && len == 0)
        return EOF;

    dst[len] = '\0';
    return (int)len;
}

int scanf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vscanf(format, args);
    va_end(args);
    return ret;
}

int vscanf(const char *format, va_list args)
{
    char line[MSG_BUF_SIZE + 1];
    int len = stdio_read_line(line, sizeof(line));
    if (len == EOF)
        return EOF;
    return stdio_vsscanf_line(line, format, args);
}

int printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);
    return ret;
}

int vprintf(const char *format, va_list args)
{
    char buf[STDIO_PRINTF_BUF_SIZE];
    int len = vsnprintf(buf, sizeof(buf), format, args);
    if (len <= 0)
        return len;

    size_t out_len = (size_t)len;
    if (out_len >= sizeof(buf))
        out_len = sizeof(buf) - 1;

    if (!StdioStream(1)) {
        UserspaceDebugLog("%s", buf);
        return len;
    }
    if (!StdioIsTty(1) || StdioMode() != TTY_MODE_RAW) {
        stdio_write(buf, out_len);
        return len;
    }
    size_t start = 0;
    for (size_t i = 0; i < out_len; i++) {
        if (buf[i] != '\n')
            continue;
        stdio_write(buf + start, i - start);
        stdio_write("\r\n", 2);
        start = i + 1;
    }
    stdio_write(buf + start, out_len - start);
    return len;
}
