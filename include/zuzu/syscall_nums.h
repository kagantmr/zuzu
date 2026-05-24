#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

#include "err.h"

/* ---- Task lifecycle (0x00-0x0F) ---- */

#define SYS_TASK_PQUIT 0x00     /* (status) -> never returns            */
#define SYS_TASK_YIELD 0x01     /* () -> 0                              */
#define SYS_TASK_WAIT 0x03      /* (pid, &status) -> 0 or -err          */
#define SYS_GET_PID 0x04        /* () -> pid                            */
#define SYS_TASK_SLEEP 0x05     /* (duration) -> 0                      */
#define SYS_TASK_PSPAWN 0x06    /*  */
#define SYS_TASK_KICKSTART 0x07 /* ()  */
#define SYS_TASK_KILL 0x08      /* (task_handle) -> 0 or -err */
#define SYS_TASK_TMAKE 0x09     /* (entry, user_sp, arg) -> tid */
#define SYS_TASK_TJOIN 0x0A     /* (tid) -> exit_status or -err */
#define SYS_TASK_TQUIT 0x0B     /* (status) -> never returns */

/* ---- IPC (0x10-0x1F) ---- */

#define SYS_PROC_SEND 0x10    /* (port, r1-r3) -> 0 or -err          */
#define SYS_PROC_RECV 0x11    /* (port) -> sender pid, r1-r3 payload  */
#define SYS_PROC_CALL 0x12    /* (port, r1-r3) -> r0-r3 reply        */
#define SYS_PROC_REPLY 0x13   /* (r0-r3) -> 0 or -err                */
#define SYS_PROC_SENDX 0x14   /* (port, buf_len) -> 0 or -err; data in shared buffer */
#define SYS_PROC_CALLX 0x15   /* (port, buf_len) -> r0=0, r1=recv_len; reply in shared buffer */
#define SYS_PROC_REPLYX 0x16  /* (reply_handle, buf_len) -> 0 or -err; data in shared buffer */
#define SYS_PROC_RECVANY 0x17 /* (handles_ptr, count, timeout, result_ptr) -> 0 or -err; fills result struct */

/* ---- Capabilities (0x20-0x2F) ---- */

#define SYS_EP_CREATE 0x20   /* () -> handle or -err                 */
#define SYS_CAP_DESTROY 0x21 /* (handle) -> 0 or -err                */
#define SYS_CAP_GRANT 0x22   /* (handle, pid) -> 0 or -err           */
#define SYS_NTFN_CREATE 0x23  /* () -> handle or -err                 */
#define SYS_NTFN_SIGNAL 0x24  /* (ntfn_handle, bits) -> 0 or -err     */
#define SYS_NTFN_WAIT 0x25    /* (ntfn_handle) -> bits or -err        */
#define SYS_NTFN_POLL 0x26    /* (ntfn_handle) -> bits or -err        */

/* ---- Memory (0x30-0x3F) ---- */

#define SYS_MEMMAP 0x30   /* (addr, size, prot) -> addr or -err   */
#define SYS_MEMUNMAP 0x31 /* (addr, size) -> 0 or -err            */
#define SYS_MEMSHARE 0x32 /* (size) -> id or -err                 */
#define SYS_ATTACH 0x33   /* (id, addr) -> addr or -err           */
#define SYS_MAPDEV 0x34   /* (phys, size) -> addr or -err         */
#define SYS_DETACH 0x35   /* (phys, size) -> addr or -err         */
#define SYS_QUERYDEV 0x36 /* (handle, out_buf, len) -> irq or -err*/
#define SYS_MPROTECT 0x37 /* (addr, size, prot) -> 0 or -err      */
#define SYS_ASINJECT 0x38 /* (args_struct_ptr) -> 0 or -err */

/* ---- Interrupts (0x40-0x4F) ---- */

#define SYS_IRQ_CLAIM 0x40 /* (irq_num) -> 0 or -err              */
#define SYS_IRQ_BIND 0x41  /* (irq_num) -> 0 or -err              */
#define SYS_IRQ_DONE 0x42  /* (irq_num) -> 0 or -err              */

/* ---- Experimental / debugging / temporary (0xF0-0xFF) ---- */

#define SYS_MAX 0xFF

#endif /* SYSCALL_NUMS_H */
