#include <stdio.h>
#include <mem.h>

#ifdef __KERNEL__
#include "core/kprintf.h"
#else
#include <zuzu/zuzu.h>
#include <zuzu/protocols/nt_protocol.h>
#include <zuzu/ipcx.h>
#endif

#define STDIO_PRINTF_BUF_SIZE 1024

#ifndef __KERNEL__
static int32_t stdio_tty = -1;
static char stdio_tty_name[4] = {'t', 't', 'y', '0'};
#endif


static void __attribute__((constructor)) stdio_init(void) {
#ifdef __KERNEL__
    // Nothing to do for kernel, kprintf is always available
#else
    stdio_register_zuart();
#endif
}

static void __attribute__((destructor)) stdio_fini(void) {
#ifndef __KERNEL__
    stdio_tty = -1;
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
    return stdio_register_zuart();
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

int stdio_register_zuart(void)
{
#ifdef __KERNEL__
    return -1;
#else
    if (stdio_tty >= 0) {
        return 0;
    }

    zuzu_ipcmsg_t lu = _call(NT_PORT, NT_LOOKUP, nt_pack(stdio_tty_name), 0);
    if ((int32_t)lu.r1 != NT_LU_OK) {
        return -1;
    }

    stdio_tty = (int32_t)lu.r2;
    if (stdio_tty < 0)
        return -1;
    return 0;
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
        if (stdio_register_zuart() == 0) {
            if (out_len > IPCX_BUF_SIZE) {
                out_len = IPCX_BUF_SIZE;
            }
            ipcx_write(buf, (uint32_t)out_len);
            (void)_sendx(stdio_tty, (uint32_t)out_len);
        }
#endif
    }
    return len;
}
