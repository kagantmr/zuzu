#ifndef ZUZU_MEM_H
#define ZUZU_MEM_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <spawn_args.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Memory management syscalls ---- */

static inline shmem_result_t _memshare(uint32_t size) {
    register uint32_t r0 __asm__("r0") = size;
    register uint32_t r1 __asm__("r1");
    __asm__ volatile("svc %[num]"
        : "+r"(r0), "=r"(r1)
        : [num] "i"(SYS_MEMSHARE)
        : "memory");
    return (shmem_result_t){.handle = (int32_t)r0, .addr = (void *)r1};
}

/* _attach/_memmap/_mapdev return a mapped VA, or a small negative errno cast
   to a pointer. The top page of the address space is the error band, so a valid
   VA (even one with the high bit set) is never misread as an error. */
static inline int _ptr_is_err(const void *p) {
    return (uintptr_t)p >= (uintptr_t)(-4095);
}

static inline void *_attach(handle_t handle) {
    register handle_t r0 __asm__("r0") = handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_ATTACH)
        : "memory");
    return (void *)r0;
}

static inline void *_mapdev(handle_t handle) {
    register handle_t r0 __asm__("r0") = handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_MAPDEV)
        : "memory");
    return (void *)r0;
}

static inline int32_t _querydev(handle_t handle, void *out_buf, uint32_t len) {
    register handle_t r0 __asm__("r0") = handle;
    register uintptr_t r1 __asm__("r1") = (uintptr_t)out_buf;
    register uint32_t r2 __asm__("r2") = len;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), [num] "i"(SYS_QUERYDEV)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _asinject(const asinject_args_t *args) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t)args;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_ASINJECT)
        : "memory");
    return (int32_t)r0;
}

static inline void *_memmap(void *addr_hint, size_t size, uint32_t prot) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t)addr_hint;
    register size_t r1 __asm__("r1") = size;
    register uint32_t r2 __asm__("r2") = prot;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), [num] "i"(SYS_MEMMAP)
        : "memory");
    return (void *)r0;
}

static inline int32_t _memunmap(void *addr, size_t size) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t)addr;
    register size_t r1 __asm__("r1") = size;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_MEMUNMAP)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _mprotect(void *addr, size_t size, uint32_t prot) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t)addr;
    register size_t r1 __asm__("r1") = size;
    register uint32_t r2 __asm__("r2") = prot;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), [num] "i"(SYS_MPROTECT)
        : "memory");
    return (int32_t)r0;
}

#ifdef __cplusplus
}
#endif

#endif
