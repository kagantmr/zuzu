#ifndef ZUZU_MEM_H
#define ZUZU_MEM_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include "zuzu/memprot.h"
#include <spawn_args.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Memory management syscalls ---- */

/* _attach/_memmap/_mapdev return a mapped VA, or a small negative errno cast
   to a pointer. The top page of the address space is the error band, so a valid
   VA (even one with the high bit set) is never misread as an error. */
static inline int zuzu_is_err(const void *p) {
    return (uintptr_t)p >= (uintptr_t)(-4095);
}

static inline void *zuzu_memmap(handle_t handle, size_t size, uint32_t prot, uint32_t flags) {
    register handle_t  r0 __asm__("r0") = handle;
    register size_t    r1 __asm__("r1") = size;
    register uint32_t  r2 __asm__("r2") = prot;
    register uint32_t  r3 __asm__("r3") = flags;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), [num] "i"(SYS_MEMMAP)
        : "memory");
    return (void *)r0;
}

static inline handle_t zuzu_shm_create(uint32_t size) {
    register uint32_t r0 __asm__("r0") = size;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_SHM_CREATE)
        : "memory");
    return (handle_t)r0;
}

static inline int32_t zuzu_dev_query(handle_t handle, void *out_buf, uint32_t len) {
    register handle_t r0 __asm__("r0") = handle;
    register uintptr_t r1 __asm__("r1") = (uintptr_t)out_buf;
    register uint32_t r2 __asm__("r2") = len;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), [num] "i"(SYS_DEV_QUERY)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t zuzu_asinject(handle_t task_handle, uintptr_t dst_va,
                                const void *src_buf, size_t len, uint32_t prot) {
    asinject_args_t args = {
        .size        = sizeof(asinject_args_t),
        .task_handle = task_handle,
        .dst_va      = dst_va,
        .src_buf     = src_buf,
        .len         = len,
        .prot        = prot,
    };
    register uintptr_t r0 __asm__("r0") = (uintptr_t)&args;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_ASINJECT)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t zuzu_memunmap(void *addr) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t)addr;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_MEMUNMAP)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t zuzu_memprotect(void *addr, size_t size, uint32_t prot) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t)addr;
    register size_t r1 __asm__("r1") = size;
    register uint32_t r2 __asm__("r2") = prot;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), [num] "i"(SYS_MEMPROTECT)
        : "memory");
    return (int32_t)r0;
}

#ifdef __cplusplus
}
#endif

#endif
