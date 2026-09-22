#ifndef _ENSURE_MACRO_H
#define _ENSURE_MACRO_H

#include <compiler.h>
#include "core/log.h"

/* Base form: run `action` if `cond` is false. Doesn't assume a return
 * convention, so it fits both the goto-cleanup style (ProcessCreate,
 * SpaceDestroy) and the arch_reg_set+return style (every Svc* handler). */
#define ENSURE(cond, action)     \
    do {                        \
        if (unlikely(!(cond))) { \
            action;             \
        }                       \
    } while (0)

/* Shorthand for the plain early-return guard clause, by far the most
 * common shape in the codebase (`if (!p) return NULL;`, `if (!task) return;`). */
#define ENSURE_RET(cond, retval) ENSURE(cond, return (retval))

/* Shorthand for the goto-cleanup chains (fail_kstack, fail_handles, etc.) */
#define ENSURE_GOTO(cond, label) ENSURE(cond, goto label)

/* Shorthand for the syscall-handler pattern specifically:
 * if (!cond) { arch_reg_set(frame, 0, err); return; } */
#define ENSURE_ERR(frame, cond, err)                        \
    ENSURE(cond, arch_reg_set((frame), 0, (err)); return)

/* Logging variant for internal invariants that "shouldn't happen" and
 * are worth a trace when they do, as opposed to routine userspace-input
 * validation (ENSURE_ERR) which fires constantly on ordinary bad input
 * and shouldn't spam the log. Stringifies the condition automatically,
 * same as a typical assert(). */
#define ENSURE_LOG(cond, action)                             \
    ENSURE(cond, KERROR("ENSURE failed: %s", #cond); action)

#endif /* _ENSURE_MACRO_H */
