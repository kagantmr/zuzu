#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

#include "err.h"

/* ---- Process/Thread lifecycle (0x00-0x0F) ---- */
#define SYS_PQUIT         0x00  /* (status) -> never returns */
#define SYS_YIELD         0x01  /* () -> 0 */
#define SYSCALL_RESERVED1 0x02  /* Reserved */
#define SYS_WAIT          0x03  /* (pid, &status) -> 0 or -err */
#define SYS_GET_PID       0x04  /* () -> pid */
#define SYS_SLEEP         0x05  /* (duration_ms) -> 0 */
#define SYS_PSPAWN        0x06  /* (name) -> empty process (0) or -err */
#define SYS_KICKSTART     0x07  /* (handle, entry, sp, reg1, reg2) -> 0 or -err */
#define SYS_PKILL         0x08  /* (task_handle) -> 0 or -err */
#define SYS_TMAKE         0x09  /* (entry, user_sp, arg) -> tid */
#define SYS_TJOIN         0x0A  /* (tid) -> exit_status or -err */
#define SYS_TQUIT         0x0B  /* (status) -> never returns */

/* ---- Messaging (0x10-0x1F) ---- */
#define SYS_MSG_SEND      0x10  /* (port, r1-r3) -> 0 or -err */
#define SYS_MSG_RECV      0x11  /* (port) -> sender, r1-r3 payload */
#define SYS_MSG_CALL      0x12  /* (port, r1-r3) -> r0-r3 reply */
#define SYS_MSG_REPLY     0x13  /* (r0-r3) -> 0 or -err */
#define SYS_MSG_LSEND     0x14  /* (port, len) -> 0 or -err; data in msg buffer */
#define SYS_MSG_LCALL     0x15  /* (port, len) -> r0=0, r1=recv_len */
#define SYS_MSG_LREPLY    0x16  /* (reply_handle, len) -> 0 or -err */
#define SYS_WAITANY       0x17  /* (handles*, count, timeout, result*) -> 0 or -err */

/* ---- Handles/Capabilities (0x20-0x2F) ---- */
#define SYS_PORT_CREATE   0x20  /* () -> handle or -err */
#define SYS_NTFN_CREATE   0x21  /* () -> handle or -err */
#define SYS_DEV_QUERY     0x22  /* (name/class, out*) -> handle or -err */
#define SYS_GRANT         0x23  /* (handle, pid) -> 0 or -err; any type */
#define SYS_DESTROY       0x24  /* (handle) -> 0 or -err; refuses REPLY/TASK, ERR_BUSY if mapped */
#define SYS_NTFN_SIGNAL   0x25  /* (handle, bits) -> 0 or -err; bits are 31-bit, bit 31 rejected */
#define SYS_NTFN_WAIT     0x26  /* (handle, timeout_ms) -> bits or -err; bit 31 reserved for errors */

/* ---- Memory (0x30-0x3F) ---- */
#define SYS_MEMMAP        0x30  /* (handle|HANDLE_ANON, size, prot, flags) -> va or -err */
#define SYS_MEMUNMAP      0x31  /* (addr, size) -> 0 or -err */
#define SYS_SHM_CREATE    0x32  /* (size) -> handle or -err */
/* 0x33 (attach) and 0x34 (mapdev) retired: folded into SYS_MEMMAP */
#define SYS_DETACH        0x35  /* (shmem handle) -> 0 or -err */
#define SYS_QUERYDEV      0x36  /* (device handle, out*, len) -> irq or -err */
#define SYS_MPROTECT      0x37  /* (addr, size, prot) -> 0 or -err */
#define SYS_ASINJECT      0x38  /* (args*) -> 0 or -err; args->size first field */

/* ---- Interrupts (0x40-0x4F) ---- */
#define SYS_IRQ_CLAIM     0x40
#define SYS_IRQ_BIND      0x41
#define SYS_IRQ_DONE      0x42

/* ---- Experimental / debugging / temporary (0xF0-0xFF) ---- */

/* --- Temporary labels - delete before merge --- */
#define SYS_TASK_PQUIT    SYS_PQUIT
#define SYS_TASK_KILL     SYS_PKILL
#define SYS_TASK_YIELD    SYS_YIELD
#define SYS_TASK_WAIT     SYS_WAIT
#define SYS_TASK_SLEEP    SYS_SLEEP
#define SYS_TASK_PSPAWN   SYS_PSPAWN
#define SYS_TASK_KICKSTART SYS_KICKSTART
#define SYS_TASK_TMAKE    SYS_TMAKE
#define SYS_TASK_TJOIN    SYS_TJOIN
#define SYS_TASK_TQUIT    SYS_TQUIT
#define SYS_PROC_SEND     SYS_MSG_SEND
#define SYS_PROC_RECV     SYS_MSG_RECV
#define SYS_PROC_CALL     SYS_MSG_CALL
#define SYS_PROC_REPLY    SYS_MSG_REPLY
#define SYS_PROC_SENDX    SYS_MSG_LSEND
#define SYS_PROC_CALLX    SYS_MSG_LCALL
#define SYS_PROC_REPLYX   SYS_MSG_LREPLY
#define SYS_PROC_RECVANY  SYS_WAITANY
#define SYS_EP_CREATE     SYS_PORT_CREATE
#define SYS_CAP_DESTROY   SYS_DESTROY
#define SYS_CAP_GRANT     SYS_GRANT
#define SYS_MEMSHARE      SYS_SHM_CREATE


#define SYS_MAX 0xFF

#endif /* SYSCALL_NUMS_H */
