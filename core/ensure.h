#ifndef _ENSURE_MACRO_H
#define _ENSURE_MACRO_H

#include "core/log.h"
#include <compiler.h>

#define ENSURE(cond, action)                                                                       \
    do {                                                                                           \
        if (unlikely(!(cond))) {                                                                   \
            action;                                                                                \
        }                                                                                          \
    } while (0)

#define ENSURE_RET(cond, retval) ENSURE(cond, return (retval))

#define ENSURE_GOTO(cond, label) ENSURE(cond, goto label)

#define ENSURE_ERR(frame, cond, err) ENSURE(cond, ArchSetInFrame((frame), 0, (err)); return)

#endif /* _ENSURE_MACRO_H */
