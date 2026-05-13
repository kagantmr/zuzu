#ifndef ZUZU_TASK_H
#define ZUZU_TASK_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Process constants ---- */

#define NAMETABLE_PID 1
#define WNOHANG (1 << 0)

/* ---- Task lifecycle syscalls ---- */

static inline void _quit(int32_t status) {
    register int32_t r0 __asm__("r0") = status;
    __asm__ volatile("svc %[num]"
        :
        : "r"(r0), [num] "i"(SYS_TASK_QUIT)
        : "memory");
    __builtin_unreachable();
}

static inline int32_t _yield(void) {
    register int32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_TASK_YIELD)
        : "memory");
    return r0;
}

static inline int32_t _wait(zpid_t pid, int32_t *status_out, uint32_t flags) {
    register zpid_t r0 __asm__("r0") = pid;
    register int32_t *r1 __asm__("r1") = status_out;
    register uint32_t r2 __asm__("r2") = flags;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), [num] "i"(SYS_TASK_WAIT)
        : "memory");
    return r0;
}

static inline int32_t _getpid(void) {
    register int32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_GET_PID)
        : "memory");
    return r0;
}

static inline int32_t _sleep(uint32_t ms) {
    register uint32_t r0 __asm__("r0") = ms;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_TASK_SLEEP)
        : "memory");
    return (int32_t) r0;
}

static inline tspawn_result_t _tspawn(const char* name) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t) name;
    register zpid_t r1 __asm__("r1"); // pid
    __asm__ volatile("svc %[num]"
    : "+r"(r0), "=r"(r1)
    : [num] "i"(SYS_TASK_TSPAWN)
    : "memory");
    return (tspawn_result_t) {.task_handle = (handle_t) r0, .pid = r1};
}

static inline handle_t _kickstart(const void *args_struct) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t) args_struct;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_TASK_KICKSTART)
        : "memory");
    return (handle_t) r0;
}

static inline int32_t _kill(handle_t task_handle) {
    register handle_t r0 __asm__("r0") = task_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_TASK_KILL)
        : "memory");
    return r0;
}

static inline tid_t _makethread(void) {
    register tid_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_TASK_MAKETHREAD)
        : "memory");
    return r0;
}

static inline int32_t _join(tid_t tid) {
    register tid_t r0 __asm__("r0") = tid;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_TASK_JOIN)
        : "memory");
    return r0;
}

/* ---- Thread-local storage ---- */

/**
 * _get_thread_ptr - Read thread pointer from TPIDRUR register
 * 
 * Returns the thread pointer set by the kernel via TPIDRUR (cp15, 0, c13, c0, 2).
 * This is typically the address of the thread_t structure in the kernel,
 * useful for thread-local storage and identifying the current thread.
 */
static inline vaddr_t _get_thread_ptr(void) {
    register vaddr_t r0 __asm__("r0");
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 2" : "=r"(r0));
    return r0;
}

#ifdef __cplusplus
}
#endif

#endif
