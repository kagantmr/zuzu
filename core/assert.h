#ifndef ASSERT_H
#define ASSERT_H

#include "panic.h"
#include "kprintf.h"

#ifdef NDEBUG
// In release builds, disable assertions
#define kassert(expr) ((void)0)
#else
// In debug builds, enable assertions
#define kassert(expr)                                        \
    do                                                       \
    {                                                        \
        if (!(expr))                                         \
        {                                                    \
            kprintf("Assertion failed: %s\nLocation: %s:%d", \
                    #expr, __FILE__, __LINE__);              \
            panic("An assertion failed");                    \
        }                                                    \
    } while (0)

#endif
#endif