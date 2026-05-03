#ifndef ZUZU_H
#define ZUZU_H

#include "zuzu/syscall_nums.h"
#include <stddef.h>
#include <stdint.h>
#include <spawn_args.h>

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

typedef struct {
    int32_t task_handle;
    uint32_t pid;
} tspawn_result_t;

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
/* legacy spawn/syscall wrappers removed (use tspawn/kickstart or loader service)
   _spawn and _spawnv were removed in favor of safer spawn paths */

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

static inline tspawn_result_t _tspawn(const char* name) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t) name;
    register uint32_t  r1 __asm__("r1"); // pid
    __asm__ volatile("svc %[num]"
    : "+r"(r0), "=r"(r1)
    : [num] "i"(SYS_TASK_TSPAWN)
    : "memory");
    return (tspawn_result_t) {.task_handle = (int32_t) r0, .pid = r1};
}

static inline int32_t _kickstart(const void *args_struct) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t) args_struct;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_TASK_KICKSTART)
        : "memory");
    return (int32_t) r0;
}

static inline int32_t _kill(int32_t task_handle) {
    register int32_t r0 __asm__("r0") = task_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_TASK_KILL)
        : "memory");
    return r0;
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

/*
 * (port) -> recv tuple in r0-r3
 * Current kernel ABI:
 * - IRQ delivery:      r0 = 0,            r1 = irq_num
 * - proc_send source:  r0 = sender_pid,   r1-r3 = payload
 * - proc_call source:  r0 = reply_handle, r1 = sender_pid, r2-r3 = payload
 */
static inline zuzu_ipcmsg_t _recv(int32_t port) {
    register int32_t r0 __asm__("r0") = port;
    register uint32_t r1 __asm__("r1") = 0;
    register uint32_t r2 __asm__("r2");
    register uint32_t r3 __asm__("r3");
    __asm__ volatile("svc %[num]"
        : "+r"(r0), "=r"(r1), "=r"(r2), "=r"(r3)
        : [num] "i"(SYS_PROC_RECV)
        : "memory");
    return (zuzu_ipcmsg_t){.r0 = r0, .r1 = r1, .r2 = r2, .r3 = r3};
}

static inline zuzu_ipcmsg_t _recv_timeout(int32_t port, uint32_t timeout_ms) {
    register int32_t r0 __asm__("r0") = port;
    register uint32_t r1 __asm__("r1") = timeout_ms;
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

/* (reply_handle, w1-w3) -> 0 or -err (r0) */
static inline int32_t _reply(uint32_t reply_handle, uint32_t w1, uint32_t w2, uint32_t w3) {
    register uint32_t r0 __asm__("r0") = reply_handle;
    register uint32_t r1 __asm__("r1") = w1;
    register uint32_t r2 __asm__("r2") = w2;
    register uint32_t r3 __asm__("r3") = w3;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), [num] "i"(SYS_PROC_REPLY)
        : "memory");
    return (int32_t) r0;
}

static inline int32_t _sendx(int32_t port, uint32_t buf_len) {
    register int32_t  r0 __asm__("r0") = port;
    register uint32_t r1 __asm__("r1") = buf_len;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_PROC_SENDX)
        : "memory");
    return r0;
}

static inline zuzu_ipcmsg_t _callx(int32_t port, uint32_t buf_len) {
    register int32_t  r0 __asm__("r0") = port;
    register uint32_t r1 __asm__("r1") = buf_len;
    register uint32_t r2 __asm__("r2");
    register uint32_t r3 __asm__("r3");
    __asm__ volatile("svc %[num]"
        : "+r"(r0), "+r"(r1), "=r"(r2), "=r"(r3)
        : [num] "i"(SYS_PROC_CALLX)
        : "memory");
    return (zuzu_ipcmsg_t){.r0 = r0, .r1 = r1, .r2 = r2, .r3 = r3};
}

static inline int32_t _replyx(uint32_t reply_handle, uint32_t buf_len) {
    register uint32_t r0 __asm__("r0") = reply_handle;
    register uint32_t r1 __asm__("r1") = buf_len;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_PROC_REPLYX)
        : "memory");
    return (int32_t)r0;
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

/* ---- Notifications ---- */

static inline int32_t _ntfn_create(void) {
    register int32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_NTFN_CREATE)
        : "memory");
    return r0;
}

static inline int32_t _ntfn_signal(uint32_t ntfn_handle, uint32_t bits) {
    register uint32_t r0 __asm__("r0") = ntfn_handle;
    register uint32_t r1 __asm__("r1") = bits;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_NTFN_SIGNAL)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _ntfn_wait(uint32_t ntfn_handle) {
    register uint32_t r0 __asm__("r0") = ntfn_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_NTFN_WAIT)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _ntfn_poll(uint32_t ntfn_handle) {
    register uint32_t r0 __asm__("r0") = ntfn_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_NTFN_POLL)
        : "memory");
    return (int32_t)r0;
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

static inline int32_t _asinject(const void *args_struct) {
    register uintptr_t r0 __asm__("r0") = (uintptr_t) args_struct;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_ASINJECT)
        : "memory");
    return (int32_t) r0;
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

/* Query device metadata for a device handle.
 * Returns IRQ number in r0 on success, or -err on failure.
 */
static inline int32_t _querydev(uint32_t dev_handle, char *out_buf, uint32_t buf_len) {
    register uint32_t r0 __asm__("r0") = dev_handle;
    register char *r1 __asm__("r1") = out_buf;
    register uint32_t r2 __asm__("r2") = buf_len;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), [num] "i"(SYS_QUERYDEV)
        : "memory");
    return (int32_t)r0;
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

static inline int32_t _irq_bind(uint32_t dev_handle, uint32_t ntfn_handle) {
    register uint32_t r0 __asm__("r0") = dev_handle;
    register uint32_t r1 __asm__("r1") = ntfn_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_IRQ_BIND)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _irq_done(uint32_t dev_handle) {
    register uint32_t r0 __asm__("r0") = dev_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_IRQ_DONE)
        : "memory");
    return (int32_t) r0;
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
