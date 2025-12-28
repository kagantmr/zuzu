#ifndef ASSERT_H
#define ASSERT_H

#include "log.h"

#ifdef NDEBUG
    // In release builds, disable assertions
    #define kassert(expr) ((void)0)
#else
    // In debug builds, enable assertions
#define kassert(expr)                                                     \
    do {                                                                  \
        if (!(expr)) {                                                    \
            KPANIC("Assertion failed: %s\nLocation: %s:%d",               \
                   #expr, __FILE__, __LINE__);                            \
        }                                                                 \
    } while (0)

#endif
#endif