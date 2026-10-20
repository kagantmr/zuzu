/**
 * @file ensure.h
 * @brief Ensure macro definitions.
 */

#ifndef CORE_ENSURE_H
#define CORE_ENSURE_H

#include "core/log.h"
#include <compiler.h>

/**
 * @brief Ensures that @p cond is true, otherwise does @p action
 *
 * @param[in] cond  The condition to check.
 * @param[in] action The action to take.
 */
#define ENSURE(cond, action)                                                                       \
    do {                                                                                           \
        if (unlikely(!(cond))) {                                                                   \
            action;                                                                                \
        }                                                                                          \
    } while (0)

/**
 * @brief ENSURE wrapper with a return value.
 */
#define ENSURE_RET(cond, retval) ENSURE(cond, return (retval))

/**
 * @brief ENSURE wrapper with a GOTO statement.
 */
#define ENSURE_GOTO(cond, label) ENSURE(cond, goto label)

/**
 * @brief ENSURE wrapper with a frame error fill then return. Used in svcs.
 */
#define ENSURE_ERR(frame, cond, err) ENSURE(cond, ArchSetInFrame((frame), 0, (err)); return)

#endif /* CORE_ENSURE_H */
