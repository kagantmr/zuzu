#ifndef _ENSURE_MACRO_H
#define _ENSURE_MACRO_H

#include "core/log.h"
#include <compiler.h>

/* Base form: run `action` if `cond` is false. Doesn't assume a return
 * convention, so it fits both the goto-cleanup style (SpaceCreate,
 * SpaceDestroy) and the ArchSetInFrame+return style (every Svc* handler). */
#define ENSURE(cond, action)                                                                       \
    do {                                                                                           \
        if (unlikely(!(cond))) {                                                                   \
            action;                                                                                \
        }                                                                                          \
    } while (0)

/* Shorthand for the plain early-return guard clause, by far the most
 * common shape in the codebase (`if (!p) return NULL;`, `if (!task) return;`). */
#define ENSURE_RET(cond, retval) ENSURE(cond, return (retval))

/* Shorthand for the goto-cleanup chains (fail_as, fail_handles, etc.) */
#define ENSURE_GOTO(cond, label) ENSURE(cond, goto label)

/* Shorthand for the syscall-handler pattern specifically:
 * if (!cond) { ArchSetInFrame(frame, 0, err); return; } */
#define ENSURE_ERR(frame, cond, err) ENSURE(cond, ArchSetInFrame((frame), 0, (err)); return)

#endif /* _ENSURE_MACRO_H */
