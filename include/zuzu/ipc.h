#ifndef ZUZU_IPC_H
#define ZUZU_IPC_H

#include "zuzu/syscall_nums.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- IPC message types ---- */

typedef struct
{
    int32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
} zuzu_ipcmsg_t;

/* ---- IPC syscalls ---- */

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
    register int32_t r0 __asm__("r0") = port;
    register uint32_t r1 __asm__("r1") = buf_len;
    __asm__ volatile("svc %[num]"
        : "+r"(r0), "+r"(r1)
        : [num] "i"(SYS_PROC_CALLX)
        : "memory");
    return (zuzu_ipcmsg_t){.r0 = r0, .r1 = r1, .r2 = 0, .r3 = 0};
}

static inline int32_t _replyx(uint32_t reply_handle, uint32_t buf_len) {
    register uint32_t r0 __asm__("r0") = reply_handle;
    register uint32_t r1 __asm__("r1") = buf_len;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_PROC_REPLYX)
        : "memory");
    return (int32_t) r0;
}

/* ---- Port syscalls ---- */

static inline int32_t _port_create(void) {
    register int32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_PORT_CREATE)
        : "memory");
    return r0;
}

static inline int32_t _port_grant(int32_t port, int32_t pid) {
    register int32_t r0 __asm__("r0") = port;
    register int32_t r1 __asm__("r1") = pid;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_PORT_GRANT)
        : "memory");
    return r0;
}

#ifdef __cplusplus
}
#endif

#endif
