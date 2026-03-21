#ifndef ZUZU_H
#define ZUZU_H

#include "zuzu/syscall_nums.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Common user ABI types ---- */

typedef struct
{
    int32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
} zuzu_ipcmsg_t;

typedef struct
{
    int32_t handle;
    void *addr;
} shmem_result_t;

typedef struct
{
    int32_t err;
    uint32_t addr;
    uint32_t size;
} dtb_reg_result_t;

/* ---- Process constants ---- */

#define NAMETABLE_PID 1
#define WNOHANG (1 << 0)

/* ---- Task lifecycle ---- */

static inline void _quit(int status) {
    register int32_t r0 __asm__("r0") = (int32_t) status;
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

static inline int32_t _spawn(const char *name, size_t len) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t) name;
    register size_t r1 __asm__("r1") = len;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_TASK_SPAWN)
        : "memory");
    return (int32_t) r0; /* pid or -err */
}

static inline int32_t _wait(int32_t pid, int32_t *status_out, uint32_t flags) {
    register int32_t r0 __asm__("r0") = pid;
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

/* ---- IPC ---- */

/* (port, r1-r3) -> 0 or -err */
static inline int32_t _send(int32_t port, uint32_t w1, uint32_t w2, uint32_t w3) {
    register int32_t r0 __asm__("r0") = port;
    register uint32_t r1 __asm__("r1") = w1;
    register uint32_t r2 __asm__("r2") = w2;
    register uint32_t r3 __asm__("r3") = w3;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), [num] "i"(SYS_PROC_SEND)
        : "memory");
    return r0;
}

/* (port) -> sender pid in r0, payload in r1-r3 */
static inline zuzu_ipcmsg_t _recv(int32_t port) {
    register int32_t r0 __asm__("r0") = port;
    register uint32_t r1 __asm__("r1");
    register uint32_t r2 __asm__("r2");
    register uint32_t r3 __asm__("r3");
    __asm__ volatile("svc %[num]"
        : "+r"(r0), "=r"(r1), "=r"(r2), "=r"(r3)
        : [num] "i"(SYS_PROC_RECV)
        : "memory");
    return (zuzu_ipcmsg_t){.r0 = r0, .r1 = r1, .r2 = r2, .r3 = r3};
}

/* (port, r1-r3) -> r0-r3 reply */
static inline zuzu_ipcmsg_t _call(int32_t port, uint32_t w1, uint32_t w2, uint32_t w3) {
    register int32_t r0 __asm__("r0") = port;
    register uint32_t r1 __asm__("r1") = w1;
    register uint32_t r2 __asm__("r2") = w2;
    register uint32_t r3 __asm__("r3") = w3;
    __asm__ volatile("svc %[num]"
        : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
        : [num] "i"(SYS_PROC_CALL)
        : "memory");
    return (zuzu_ipcmsg_t){.r0 = r0, .r1 = r1, .r2 = r2, .r3 = r3};
}

/* (r0-r3) -> 0 or -err (r0) */
static inline int32_t _reply(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r3 __asm__("r3") = a3;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), [num] "i"(SYS_PROC_REPLY)
        : "memory");
    return (int32_t) r0;
}

/* ---- Ports ---- */

static inline int32_t _port_create(void) {
    register int32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_PORT_CREATE)
        : "memory");
    return r0; /* handle or -err */
}

static inline int32_t _port_destroy(int32_t handle) {
    register int32_t r0 __asm__("r0") = handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_PORT_DESTROY)
        : "memory");
    return r0;
}

static inline int32_t _port_grant(int32_t handle, int32_t pid) {
    register int32_t r0 __asm__("r0") = handle;
    register int32_t r1 __asm__("r1") = pid;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_PORT_GRANT)
        : "memory");
    return r0;
}

/* ---- Memory ---- */

/* (addr, size, prot) -> addr or -err */
static inline void *_memmap(void *addr, size_t size, uint32_t prot) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t) addr;
    register size_t r1 __asm__("r1") = size;
    register uint32_t r2 __asm__("r2") = prot;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), [num] "i"(SYS_MEMMAP)
        : "memory");
    return (void *) r0; /* mapped addr or (void*)-err */
}

static inline int32_t _memunmap(void *addr, size_t size) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t) addr;
    register size_t r1 __asm__("r1") = size;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_MEMUNMAP)
        : "memory");
    return (int32_t) r0;
}

static inline shmem_result_t _memshare(size_t size) {
    register size_t    r0 __asm__("r0") = size;
    register uintptr_t r1 __asm__("r1");
    __asm__ volatile("svc %[num]"
                     : "+r"(r0), "=r"(r1)
                     : [num] "i"(SYS_MEMSHARE)
                     : "memory");
    return (shmem_result_t){ .handle = (int32_t)r0, .addr = (void*)r1 };
}

/* (handle_idx) -> addr or -err */
static inline void *_attach(int32_t handle_idx) {
    register int32_t r0 __asm__("r0") = handle_idx;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_ATTACH)
        : "memory");
    return (void *) (uintptr_t) r0;
}

/* ---- Devices ---- */

/* Map a device handle into the calling process's address space */
static inline void *_mapdev(uint32_t handle) {
    register uint32_t r0 __asm__("r0") = handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_MAPDEV)
        : "memory");
    return (void *)r0;
}

/* (handle) -> 0 or -err */
static inline int32_t _detach(int32_t handle) {
    register int32_t r0 __asm__("r0") = handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_DETACH)
        : "memory");
    return r0;
}

/* ---- Interrupts ---- */

static inline int32_t _irq_claim(uint32_t dev_handle) {
    register uint32_t r0 __asm__("r0") = dev_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_IRQ_CLAIM)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _irq_bind(uint32_t dev_handle, uint32_t port_handle) {
    register uint32_t r0 __asm__("r0") = dev_handle;
    register uint32_t r1 __asm__("r1") = port_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_IRQ_BIND)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _irq_done(uint32_t irq_num) {
    register uint32_t r0 __asm__("r0") = irq_num;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_IRQ_DONE)
        : "memory");
    return (int32_t) r0;
}

/* ---- Debug/diagnostics ---- */

static inline int32_t _log(const char *str, size_t len) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t) str;
    register size_t r1 __asm__("r1") = len;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_LOG)
        : "memory");
    return (int32_t) r0;
}

static inline int32_t _dump(void) {
    register int32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_DUMP)
        : "memory");
    return r0;
}

static inline uint32_t _pmm_free(void) {
    register uint32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_PMM_GETFREE)
        : "memory");
    return r0;
}

/* ---- IPC helpers ---- */

static inline uint32_t nt_pack(const char *s) {
    uint32_t v = 0;
    for (int i = 0; i < 4 && s[i]; i++)
        v |= (uint32_t) (unsigned char) s[i] << (i * 8);
    return v;
}

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_H */
