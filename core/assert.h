#ifndef ASSERT_H
#define ASSERT_H

#define kassert(expr)                                                     \
    do {                                                                  \
        if (!(expr)) {                                                    \
            panicf("Assertion failed: %s\nLocation: %s:%d",               \
                   #expr, __FILE__, __LINE__);                            \
        }                                                                 \
    } while (0)

#endif