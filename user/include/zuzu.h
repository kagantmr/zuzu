#ifndef ZUZU_H
#define ZUZU_H

#include "syscall_nums.h"
#include <stddef.h>

static inline void _quit(int status) {
    register int r0 __asm__("r0") = status;
    __asm__ volatile("svc %[num]" : : "r"(r0), [num] "i"(SYS_TASK_QUIT));
    __builtin_unreachable();
}

static inline void _yield(void) {
    __asm__ volatile("svc %[num]" : : [num] "i"(SYS_TASK_YIELD));
}

static inline void _log(const char *str, size_t len) {
    register const char *r0 __asm__("r0") = str;
    register size_t r1 __asm__("r1") = len;
    __asm__ volatile("svc %[num]" : : "r"(r0), "r"(r1), [num] "i"(SYS_LOG) : "memory");
}

static inline int _spawn(const char *name, size_t len) {
    register int r0 __asm__("r0") = (int)name;
    register size_t r1 __asm__("r1") = len;
    __asm__ volatile("svc %[num]" : "+r"(r0) : "r"(r1), [num] "i"(SYS_TASK_SPAWN) : "memory");
    return r0;
}

static inline int _wait(int pid) {
    register int r0 __asm__("r0") = pid;
    __asm__ volatile("svc %[num]" : "+r"(r0) : [num] "i"(SYS_TASK_WAIT) : "memory");
    return r0;
}

static inline int _getpid(void) {
    register int r0 __asm__("r0");
    __asm__ volatile("svc %[num]" : "=r"(r0) : [num] "i"(SYS_GET_PID));
    return r0;
}

static inline void _sleep(unsigned int ms) {
    register unsigned int r0 __asm__("r0") = ms;
    __asm__ volatile("svc %[num]" : : "r"(r0), [num] "i"(SYS_TASK_SLEEP));
}

#endif