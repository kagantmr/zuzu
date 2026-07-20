#include <stdio.h>
#include <ctype.h>
#include <mem.h>
#include <stdlib.h>

#ifdef __KERNEL__
#include "core/kprintf.h"
#else
#include <zuzu/lmsg.h>
#include <zuzu/protocols/nt_protocol.h>
#include <zuzu/zuzu.h>
#endif

#define STDIO_PRINTF_BUF_SIZE 1024

#ifndef __KERNEL__
static int32_t stdio_tty = -1;
static char stdio_tty_name[4] = {'t', 't', 'y', '0'};
static char stdio_input_buf[LMSG_BUF_SIZE];
static uint32_t stdio_input_len;
static uint32_t stdio_input_pos;
static int stdio_input_pushback = EOF;
#endif

static void __attribute__((constructor)) stdio_init(void) {
#ifdef __KERNEL__
    // Nothing to do for kernel, kprintf is always available
#else
    // Lazy initialization: don't look up tty service on startup.
    // Wait until first printf() or explicit `stdio_register_uart()` call.
#endif
}

static void __attribute__((destructor)) stdio_fini(void) {
#ifndef __KERNEL__
    stdio_tty = -1;
    stdio_input_len = 0;
    stdio_input_pos = 0;
    stdio_input_pushback = EOF;
#endif
}

int stdio_route_tty(const char name[4])
{
#ifdef __KERNEL__
    (void)name;
    return -1;
#else
    if (!name)
        return -1;

    for (int i = 0; i < 4; i++)
        stdio_tty_name[i] = name[i];
    stdio_tty = -1;
    stdio_input_len = 0;
    stdio_input_pos = 0;
    stdio_input_pushback = EOF;
    return stdio_register_uart();
#endif
}

int stdio_use_tty(uint32_t index)
{
#ifdef __KERNEL__
    (void)index;
    return -1;
#else
    char name[4] = {'t', 't', 'y', (char)('0' + (index % 10u))};
    return stdio_route_tty(name);
#endif
}

int stdio_register_uart(void)
{
#ifdef __KERNEL__
    return -1;
#else
    if (stdio_tty >= 0) {
        return 0;
    }

    msg_t lu = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack(stdio_tty_name), 0);
    if ((int32_t)lu.r1 != NT_LU_OK) {
        return -1;
    }

    stdio_tty = (int32_t)lu.r2;
    if (stdio_tty < 0)
        return -1;
    return 0;
#endif
}

static int __attribute__((unused)) stdio_refill_input(void)
{
#ifdef __KERNEL__
    return EOF;
#else
    if (stdio_register_uart() != 0)
        return EOF;

    msg_t reply = zuzu_msg_lcall(stdio_tty, LMSG_BUF_SIZE);
    if (reply.r0 < 0)
        return EOF;

    uint32_t got = reply.r1;
    if (got > LMSG_BUF_SIZE)
        got = LMSG_BUF_SIZE;
    if (got == 0)
        return EOF;

    memcpy(stdio_input_buf, lmsg_buf(), got);
    stdio_input_len = got;
    stdio_input_pos = 0;
    return 0;
#endif
}

static int __attribute__((unused)) stdio_stream_getc(void)
{
#ifdef __KERNEL__
    return EOF;
#else
    if (stdio_input_pushback != EOF) {
        int c = stdio_input_pushback;
        stdio_input_pushback = EOF;
        return c;
    }

    if (stdio_input_pos >= stdio_input_len) {
        if (stdio_refill_input() == EOF)
            return EOF;
    }

    return (unsigned char)stdio_input_buf[stdio_input_pos++];
#endif
}

int getchar(void)
{
#ifdef __KERNEL__
    return EOF;
#else
    return stdio_stream_getc();
#endif
}

static const char * __attribute__((unused)) stdio_skip_ws(const char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

/* Blocking counterpart to stdio_stream_getc.
 *
 * The tty driver's read is non-blocking: it replies with whatever is in its
 * RX ring, which between keystrokes is nothing, and stdio_refill_input turns
 * that empty reply into EOF. getchar() keeps those non-blocking semantics --
 * zzsh's input loop is built on polling it and sleeping on EOF -- but a
 * line-oriented reader must not treat "nothing typed yet" as end of input, or
 * scanf() returns EOF the moment it is called rather than waiting for a line.
 */
static int __attribute__((unused)) stdio_stream_getc_blocking(void)
{
#ifdef __KERNEL__
    return EOF;
#else
    for (;;) {
        int c = stdio_stream_getc();
        if (c != EOF)
            return c;
        zuzu_sleep(5);
    }
#endif
}

static int __attribute__((unused)) stdio_read_line(char *dst, size_t max)
{
    if (!dst || max == 0)
        return EOF;

    size_t len = 0;
    int c = EOF;

    while ((c = stdio_stream_getc_blocking()) != EOF) {
        /* Enter on a serial console sends CR, and nothing downstream turns
         * it into LF, so CR has to end the line as well -- discarding it and
         * waiting for a LF that never arrives hangs the read. A CRLF pair
         * must still count as one line ending, so swallow the LF; the peek
         * is the non-blocking getc because a lone CR must not wait around
         * for a companion that is never coming. */
        if (c == '\r') {
            int nc = stdio_stream_getc();
            if (nc != EOF && nc != '\n')
                stdio_input_pushback = nc;
            break;
        }
        if (c == '\n')
            break;
        if (len + 1 < max)
            dst[len++] = (char)c;
    }

    if (c == EOF && len == 0)
        return EOF;

    dst[len] = '\0';
    return (int)len;
}

static int __attribute__((unused)) stdio_vsscanf_line(const char *input, const char *format, va_list args)
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

        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' || conv == 'x' || conv == 'p' ||
            conv == 'f' || conv == 'e' || conv == 'g' || conv == 'a') {
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
                case 'a':
                {
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
#ifdef __KERNEL__
    (void)format;
    (void)args;
    return EOF;
#else
    char line[LMSG_BUF_SIZE + 1];
    int len = stdio_read_line(line, sizeof(line));
    if (len == EOF)
        return EOF;
    return stdio_vsscanf_line(line, format, args);
#endif
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
    if (len > 0) {
        size_t out_len = (size_t)len;
        if (out_len >= sizeof(buf)) {
            out_len = sizeof(buf) - 1;
        }

#ifdef __KERNEL__
        (void)out_len;
        kprintf("%s", buf);
#else
        if (stdio_register_uart() == 0) {
            /* buf can exceed the lmsg buffer; send in chunks */
            size_t off = 0;
            while (off < out_len) {
                uint32_t chunk = (uint32_t)(out_len - off);
                if (chunk > LMSG_BUF_SIZE)
                    chunk = LMSG_BUF_SIZE;
                lmsg_write(buf + off, chunk);
                (void)zuzu_msg_lsend(stdio_tty, chunk);
                off += chunk;
            }
        }
#endif
    }
    return len;
}
