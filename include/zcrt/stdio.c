#include <stdio.h>
#include <mem.h>

#ifdef __KERNEL__
#include "core/kprintf.h"
#else
#include <zuzu.h>
#include <zuzu/protocols/nt_protocol.h>
#include <zuzu/protocols/zuart_protocol.h>
#endif

#define STDIO_PRINTF_BUF_SIZE 1024
#define STDIO_ZUART_BUF_SIZE 4096

#ifndef __KERNEL__
static int32_t zuart_port = -1;
static int32_t shmem_handle = -1;
static char *shmem_buf = NULL;
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
    if (shmem_handle >= 0) {
        (void)_detach(shmem_handle);
    }
    shmem_handle = -1;
    shmem_buf = NULL;
    zuart_port = -1;
#endif
}

int stdio_register_zuart(void)
{
#ifdef __KERNEL__
    return -1;
#else
    if (zuart_port >= 0 && shmem_handle >= 0 && shmem_buf != NULL) {
        return 0;
    }

    zuzu_ipcmsg_t lu = _call(NT_PORT, NT_LOOKUP, nt_pack("uart"), 0);
    if (lu.r1 != NT_LU_OK) {
        return -1;
    }

    zuart_port = (int32_t)lu.r2;

    zuzu_ipcmsg_t shm_reply = _call(zuart_port, ZUART_CMD_GET_SHMEM, 0, 0);
    if ((int32_t)shm_reply.r1 != ZUART_SEND_OK) {
        return -1;
    }
    shmem_handle = (int32_t)shm_reply.r2;

    shmem_buf = (char *)_attach(shmem_handle);
    if ((intptr_t)shmem_buf <= 0) {
        shmem_buf = NULL;
        shmem_handle = -1;
        return -1;
    }

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
            if (out_len > STDIO_ZUART_BUF_SIZE) {
                out_len = STDIO_ZUART_BUF_SIZE;
            }
            memcpy(shmem_buf, buf, out_len);
            (void)_call(zuart_port, ZUART_CMD_WRITE, zuart_pack_arg(shmem_handle, (uint32_t)out_len), 0);
        }
#endif
    }
    return len;
}