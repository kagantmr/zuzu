#ifndef ZUZU_UDBG_H
#define ZUZU_UDBG_H

#include <arch/svc.h>
#include <stdint.h>
#include <zuzu/zuzu.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UDBG_ENABLED
#define UDBG_ENABLED 0
#endif

#if defined(DEBUG) && UDBG_ENABLED

#include <stdarg.h>
#include <stdio.h>

static inline void UserspaceDebugLog(const char *fmt, ...)
{
    char line[240];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    uint32_t len = (n >= (int)sizeof(line)) ? (uint32_t)sizeof(line) - 1U : (uint32_t)n;
    ArchInvokeSvc(SVC_LOG, (uint32_t)(uintptr_t)line, (Register)len, 0, 0);
}

#else

static inline void UserspaceDebugLog(const char *fmt, ...) { (void)fmt; }

#endif /* DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_UDBG_H */
