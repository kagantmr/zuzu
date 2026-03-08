#ifndef ZCRT_ASSERT_H
#define ZCRT_ASSERT_H

#ifdef NDEBUG
    #define kassert(cond) ((void)0)
#else
    #ifdef __KERNEL__
        #include <core/panic.h>
        #define kassert(cond) if(!(cond)) panic("Assertion failed: " #cond)
    #else
        #include <zuzu.h>
        // User space assert is just quit for now
        #define kassert(cond) if(!(cond)) { _quit(-1); } 
    #endif
#endif

#endif