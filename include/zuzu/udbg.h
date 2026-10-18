/**
 * @file udbg.h
 * @brief Userspace debug logging through the kernel console.
 */

#ifndef ZUZU_UDBG_H
#define ZUZU_UDBG_H

#include <arch/svc.h>
#include <stdint.h>
#include <zuzu/zuzu.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Per-file switch for UserspaceDebugLog().
 *
 * Define this to 1 before including the header to turn logging on in that file. Logging is
 * compiled in only when this is 1 and the build defines DEBUG; otherwise calls compile to nothing.
 */
#ifndef UDBG_ENABLED
#define UDBG_ENABLED 0
#endif

#if defined(DEBUG) && UDBG_ENABLED

#include <stdarg.h>
#include <stdio.h>

/**
 * @brief Format a message and send it to the kernel console.
 *
 * Messages longer than 239 characters are truncated. In builds without DEBUG, or in files that
 * do not define UDBG_ENABLED as 1, this does nothing.
 *
 * @param[in] fmt A printf-style format string.
 * @param[in] ... The values to format.
 */
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

/**
 * @brief Disabled form of UserspaceDebugLog(); does nothing.
 *
 * @param[in] fmt Ignored.
 * @param[in] ... Ignored.
 */
static inline void UserspaceDebugLog(const char *fmt, ...) { (void)fmt; }

#endif /* DEBUG */

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_UDBG_H */
