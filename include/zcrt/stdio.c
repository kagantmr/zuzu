#include <stdio.h>
#include <mem.h>

#ifdef __KERNEL__
#include "core/kprintf.h"
#else
#include <zuzu.h>
#include <zuzu/protocols/nt_protocol.h>
#include <zuzu/protocols/zuart_protocol.h>
#include <zuzu/ipcx.h>
#endif

#define STDIO_PRINTF_BUF_SIZE 1024

#ifndef __KERNEL__
static int32_t zuart_port = -1;
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
    zuart_port = -1;
#endif
}

int stdio_register_zuart(void)
{
#ifdef __KERNEL__
    return -1;
#else
    if (zuart_port >= 0) {
        return 0;
    }

    zuzu_ipcmsg_t lu = _call(NT_PORT, NT_LOOKUP, nt_pack("uart"), 0);
    if (lu.r1 != NT_LU_OK) {
        return -1;
    }

    zuart_port = (int32_t)lu.r2;
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
            memcpy((void *)IPCX_BUF_VA, buf, out_len);
            (void)_sendx(zuart_port, (uint32_t)out_len);
        }
#endif
    }
    return len;
}