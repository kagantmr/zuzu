#include <stdio.h>
#include <mem.h>

#ifdef __KERNEL__
#include "core/kprintf.h"
#else
#include <zuzu/zuzu.h>
#include <zuzu/protocols/nt_protocol.h>
#include <zuzu/protocols/zuart_protocol.h>
#include <zuzu/protocols/sysd_protocol.h>
#include <zuzu/ipcx.h>
#endif

#define STDIO_PRINTF_BUF_SIZE 1024

#ifndef __KERNEL__
static int32_t stdin_slot = -1;
static int32_t stdout_slot = -1;
static int32_t stderr_slot = -1;
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
    stdin_slot = stdout_slot = stderr_slot = -1;
#endif
}

int stdio_register_zuart(void)
{
#ifdef __KERNEL__
    return -1;
#else
    if (stdin_slot >= 0 && stdout_slot >= 0 && stderr_slot >= 0) {
        return 0;
    }

    /* Lookup sysd and request the three TTY handles */
    zuzu_ipcmsg_t lu = _call(NT_PORT, NT_LOOKUP, nt_pack("sysd"), 0);
    if ((int32_t)lu.r1 != NT_LU_OK) {
        return -1;
    }
    int32_t sysd_port = (int32_t)lu.r2;

    zuzu_ipcmsg_t r = _call(sysd_port, SYSD_GET_TTY, 0, 0);
    if ((int32_t)r.r0 < 0) {
        return -1;
    }
    /* _call reply payload is in r1/r2/r3 */
    stdin_slot = (int32_t)r.r1;
    stdout_slot = (int32_t)r.r2;
    stderr_slot = (int32_t)r.r3;

    if (stdin_slot < 0 || stdout_slot < 0 || stderr_slot < 0)
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
            (void)_sendx(stdout_slot, (uint32_t)out_len);
        }
#endif
    }
    return len;
}
