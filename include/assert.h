#ifndef ZCRT_ASSERT_H
#define ZCRT_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NDEBUG
    #define assert(cond) ((void)0)
#else
    #ifdef __ZUZU__
        #include <core/panic.h>
        #define assert(cond) \
            do { if (!(cond)) panic("Assertion failed: %s (%s:%d)", #cond, __FILE__, __LINE__); } while (0)
    #else
        #include <zuzu/zuzu.h>
        // User space assert is just quit for now
        #define assert(cond) \
            do { if (!(cond)) { ZuzuPQuit(-1); } } while (0)
    #endif
#endif

#ifdef __cplusplus
}
#endif

#endif