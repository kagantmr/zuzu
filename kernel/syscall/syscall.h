#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "arch/arm/include/context.h"
#include "stdbool.h"
#include "stddef.h"
#include "kernel/vmm/vmm.h"
#include "stdint.h"

/*
 * zuzu Syscall ABI
 *
 * Syscall numbers encoded in the lower 8 bits of SVC immediate.
 * Arguments in r0-r3, return in r0. See docs/syscall.md for full ABI.
 */

/* ---- Task lifecycle (0x00-0x0F) ---- */

#define SYS_TASK_QUIT   0x00    /* (status) -> never returns            */
#define SYS_TASK_YIELD  0x01    /* () -> 0                              */
#define SYS_TASK_SPAWN  0x02    /* (name, len) -> pid or -err           */
#define SYS_TASK_WAIT   0x03    /* (pid, &status) -> 0 or -err          */
#define SYS_GET_PID     0x04    /* () -> pid                            */
#define SYS_TASK_SLEEP  0x05    /* (duration) -> 0                      */

/* ---- IPC (0x10-0x1F) ---- */

#define SYS_PROC_SEND   0x10    /* (port, r1-r3) -> 0 or -err          */
#define SYS_PROC_RECV   0x11    /* (port) -> sender pid, r1-r3 payload  */
#define SYS_PROC_CALL   0x12    /* (port, r1-r3) -> r0-r3 reply        */
#define SYS_PROC_REPLY  0x13    /* (r0-r3) -> 0 or -err                */

/* ---- Ports (0x20-0x2F) ---- */

#define SYS_PORT_CREATE  0x20   /* () -> handle or -err                 */
#define SYS_PORT_DESTROY 0x21   /* (handle) -> 0 or -err                */
#define SYS_PORT_GRANT   0x22   /* (handle, pid) -> 0 or -err           */

/* ---- Memory (0x30-0x3F) ---- */

#define SYS_MMAP    0x30        /* (addr, size, prot) -> addr or -err   */
#define SYS_MUNMAP  0x31        /* (addr, size) -> 0 or -err            */
#define SYS_MSHARE  0x32        /* (size) -> id or -err                 */
#define SYS_ATTACH  0x33        /* (id, addr) -> addr or -err           */
#define SYS_MAPDEV  0x34        /* (phys, size) -> addr or -err         */

/* ---- Interrupts (0x40-0x4F) ---- */

#define SYS_IRQ_CLAIM   0x40    /* (irq_num) -> 0 or -err              */
#define SYS_IRQ_WAIT    0x41    /* (irq_num) -> 0 or -err              */
#define SYS_IRQ_DONE    0x42    /* (irq_num) -> 0 or -err              */

/* ---- Experimental / debugging / temporary (0xF0-0xFF) ---- */

#define SYS_LOG     0xF0        /* (msg, len) -> 0 or -err             */
#define SYS_DUMP    0xF1        /* () -> 0                             */

#define SYS_MAX     0xFF

/* ---- Error codes (returned as negative r0) ---- */

#define ERR_NOPERM      (-1)       /* Operation not permitted              */
#define ERR_NOENT       (-2)       /* Not found                            */
#define ERR_BUSY        (-3)       /* Resource busy, try again             */
#define ERR_NOMEM       (-4)       /* Out of memory                        */
#define ERR_BADFORM     (-5)       /* Bad handle                           */
#define ERR_BADARG      (-6)       /* Invalid argument                     */
#define ERR_NOMATCH     (-7)       /* Syscall not implemented              */
#define ERR_PTRFAULT    (-8)       /* Bad pointer                          */
#define ERR_DEAD        (-9)       // endpoint destroyed while waiting

void syscall_dispatch(uint8_t svc_num, exception_frame_t *frame);

static inline bool validate_user_ptr(uintptr_t addr, size_t len) {
    if (addr + len < addr) return false;
    if (addr >= USER_VA_TOP) return false;
    if (addr + len > USER_VA_TOP) return false;
    return true;
}

#endif /* KERNEL_SYSCALL_H */