/**
 * @file
 * @brief assert(): panics in the kernel, quits the program in userspace; nothing with NDEBUG.
 */

#ifndef ZCRT_ASSERT_H
#define ZCRT_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NDEBUG
/** @brief Does nothing; NDEBUG is defined. */
#define assert(cond) ((void)0)
#else
#ifdef __ZUZU__
#include <core/panic.h>
/** @brief Panics with the condition, file and line if @p cond is false. */
#define assert(cond)                                                                               \
    do {                                                                                           \
        if (!(cond))                                                                               \
            panic("Assertion failed: %s (%s:%d)", #cond, __FILE__, __LINE__);                      \
    } while (0)
#else
#include <zuzu/zuzu.h>
/** @brief Quits the program with -1 if @p cond is false. */
#define assert(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            Quit(-1);                                                                              \
        }                                                                                          \
    } while (0)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif