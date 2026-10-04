#include <ctype.h>
#include <dev/protocols/tty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/msg.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define STDIO_PRINTF_BUF_SIZE 1024
#define STDIO_BIT_KICK 0
#define STDIO_POLL_MS 20
#define STDIO_ATTACH_RETRIES 100

typedef enum {
    STDIO_IDLE,
    STDIO_READY,
    STDIO_FAILED, /* endpoint never appeared; stop retrying */
} StdioState;

static StdioState stdio_state = STDIO_IDLE;
static Handle stdio_port = -1;
static Handle stdio_event = -1;
static TtyConn stdio_conn;
static char stdio_alias[TTY_NAME_MAX];
static int stdio_pushback = EOF;
static uint32_t stdio_mode = TTY_MODE_COOKED | TTY_MODE_ECHO;

static void stdio_disconnect(void)
{
    if (stdio_state == STDIO_READY) {
        TtyShm *shm = stdio_conn.shm;
        for (int i = 0; i < STDIO_ATTACH_RETRIES && ShmRingAvail(&shm->up_hdr) > 0; i++) {
            Signal(stdio_conn.doorbell, 1U << stdio_conn.bit, false);
            WaitOn(stdio_event, STDIO_POLL_MS);
        }
        TtyClientClose(stdio_port, &stdio_conn);
    }
    stdio_state = STDIO_IDLE;
    stdio_pushback = EOF;
}

static void __attribute__((destructor)) stdio_fini(void) { stdio_disconnect(); }

/* Idempotent. Connects on first use rather than at startup so services that
 * link this file never block waiting for ttysvc. Returns 0 when attached. */
int stdio_open_tty(void)
{
    if (stdio_state == STDIO_READY)
        return 0;
    if (stdio_state == STDIO_FAILED)
        return -1;

    if (stdio_port < 0) {
        stdio_port = LookupService("/svc/tty");
        if (stdio_port < 0) {
            stdio_port = -1;
            return -1;
        }
    }
    if (stdio_event < 0) {
        stdio_event = CreateEvent();
        if (stdio_event < 0) {
            stdio_event = -1;
            return -1;
        }
    }

    Err rc = ERR_NOENT;
    for (int i = 0; i < STDIO_ATTACH_RETRIES && rc == ERR_NOENT; i++) {
        rc = TtyClientConnect(stdio_port, TTY_ATTACH, stdio_alias, stdio_event, STDIO_BIT_KICK,
                              &stdio_conn);
        if (rc == ERR_NOENT)
            Sleep(10);
    }
    if (rc != ZUZU_OK) {
        if (rc == ERR_NOENT)
            stdio_state = STDIO_FAILED;
        return -1;
    }

    rc = TtyClientSetMode(stdio_port, &stdio_conn, stdio_mode);
    if (rc != ZUZU_OK) {
        TtyClientClose(stdio_port, &stdio_conn);
        return -1;
    }
    stdio_state = STDIO_READY;
    return 0;
}

/* Raw mode hands every byte through untouched, so the caller does its own
 * echo and line editing. Output still gets "\n" -> "\r\n" (what cooked mode
 * does in ttysvc). The setting survives a reattach. */
int stdio_set_raw(int enable)
{
    stdio_mode = enable ? TTY_MODE_RAW : (TTY_MODE_COOKED | TTY_MODE_ECHO);
    if (stdio_state != STDIO_READY)
        return 0;
    return TtyClientSetMode(stdio_port, &stdio_conn, stdio_mode) == ZUZU_OK ? 0 : -1;
}

/* Releases the session so another program can take the foreground; the next
 * printf or getchar attaches again. */
void stdio_close_tty(void) { stdio_disconnect(); }

/* `name` is a ttysvc alias ("" for the default endpoint). Reconnects now so
 * a bad alias is reported here rather than at the first printf. */
int stdio_route_tty(const char *name)
{
    if (!name || strlen(name) >= TTY_NAME_MAX)
        return -1;

    stdio_disconnect();
    strcpy(stdio_alias, name);
    return stdio_open_tty();
}

int stdio_use_tty(uint32_t index)
{
    char name[TTY_NAME_MAX];
    snprintf(name, sizeof(name), "tty%u", (unsigned)(index % 10u));
    return stdio_route_tty(name);
}

static void stdio_kick(void) { Signal(stdio_conn.doorbell, 1U << stdio_conn.bit, false); }

/* Blocks while the up ring is full: that is the backpressure ttysvc applies
 * to a background consumer. */
static void stdio_write(const char *s, size_t n)
{
    TtyShm *shm = stdio_conn.shm;
    while (n > 0) {
        uint32_t w = ShmRingPush(&shm->up_hdr, shm->up_data, (const uint8_t *)s, (uint32_t)n);
        s += w;
        n -= w;
        stdio_kick();
        if (n > 0)
            WaitOn(stdio_event, STDIO_POLL_MS);
    }
}

/* Blocks until a byte arrives. Event words are only hints, so the ring and
 * the EOF flag are re-checked on every wakeup. */
static int stdio_stream_getc(void)
{
    if (stdio_pushback != EOF) {
        int c = stdio_pushback;
        stdio_pushback = EOF;
        return c;
    }

    if (stdio_open_tty() != 0)
        return EOF;

    TtyShm *shm = stdio_conn.shm;
    for (;;) {
        uint8_t b = 0;
        if (ShmRingPop(&shm->down_hdr, shm->down_data, &b, 1) == 1)
            return b;
        if (TtyEofPending(shm))
            return EOF;
        WaitOn(stdio_event, STDIO_POLL_MS);
    }
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

    if (stdio_open_tty() == 0) {
        if (stdio_mode != TTY_MODE_RAW) {
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
    } else
        UserspaceDebugLog("%s", buf);
    return len;
}
