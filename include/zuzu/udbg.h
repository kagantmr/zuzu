/** udbg.h -- DEBUG-only userspace -> kernel-console logging.
 *
 * Writes straight to the kernel console via the retired 0x02 syscall slot, so
 * it works from the very first instruction of any process -- including sysd
 * and devmgr, which run long before pl011drv exists and therefore have no tty
 * to print to. Compiled out entirely unless DEBUG is defined.
 *
 * This is a debugging aid, not ABI: it is absent from release builds and must
 * never be depended on by shipped code.
 */

#ifndef ZUZU_UDBG_H
#define ZUZU_UDBG_H

#include "zuzu/syscall_nums.h"
#include <arch/syscall.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DEBUG

#include <stdarg.h>
#include <stdio.h>

/** Emit one line to the kernel console. Truncated past ~240 bytes. */
static inline void udbg(const char *fmt, ...)
{
    char line[240];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    uint32_t len = (n >= (int)sizeof(line)) ? (uint32_t)sizeof(line) - 1u : (uint32_t)n;
    Syscall(SYS_LOG, (uint32_t)(uintptr_t)line, len, 0, 0);
}

#else /* !DEBUG */

static inline void udbg(const char *fmt, ...) { (void)fmt; }

#endif /* DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_UDBG_H */
