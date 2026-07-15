#ifndef ZCRT_ASSERT_H
#define ZCRT_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NDEBUG
    #define assert(cond) ((void)0)
#else
    #ifdef __KERNEL__
        #include <core/panic.h>
        #define assert(cond) if(!(cond)) panic("Assertion failed: " #cond)
    #else
        #include <zuzu/zuzu.h>
        // User space assert is just quit for now
        #define assert(cond) if(!(cond)) { zuzu_pquit(-1); }
    #endif
#endif

#ifdef __cplusplus
}
#endif

#endif