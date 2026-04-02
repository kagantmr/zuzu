#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

/* ---- Task lifecycle (0x00-0x0F) ---- */

#define SYS_TASK_QUIT 0x00  /* (status) -> never returns            */
#define SYS_TASK_YIELD 0x01 /* () -> 0                              */
#define SYS_TASK_SPAWN 0x02 /* (name, len) -> pid or -err           */
#define SYS_TASK_WAIT 0x03  /* (pid, &status) -> 0 or -err          */
#define SYS_GET_PID 0x04    /* () -> pid                            */
#define SYS_TASK_SLEEP 0x05 /* (duration) -> 0                      */

/* ---- IPC (0x10-0x1F) ---- */

#define SYS_PROC_SEND 0x10  /* (port, r1-r3) -> 0 or -err          */
#define SYS_PROC_RECV 0x11  /* (port) -> sender pid, r1-r3 payload  */
#define SYS_PROC_CALL 0x12  /* (port, r1-r3) -> r0-r3 reply        */
#define SYS_PROC_REPLY 0x13 /* (r0-r3) -> 0 or -err                */
#define SYS_PROC_SENDX 0x14 /* (port, buf_len) -> 0 or -err; data in shared buffer */
#define SYS_PROC_CALLX 0x15 /* (port, buf_len) -> r0=0, r1=recv_len; reply in shared buffer */
#define SYS_PROC_REPLYX 0x16 /* (reply_handle, buf_len) -> 0 or -err; data in shared buffer */

/* ---- Ports (0x20-0x2F) ---- */

#define SYS_PORT_CREATE 0x20  /* () -> handle or -err                 */
#define SYS_PORT_DESTROY 0x21 /* (handle) -> 0 or -err                */
#define SYS_PORT_GRANT 0x22   /* (handle, pid) -> 0 or -err           */

/* ---- Memory (0x30-0x3F) ---- */

#define SYS_MEMMAP 0x30   /* (addr, size, prot) -> addr or -err   */
#define SYS_MEMUNMAP 0x31 /* (addr, size) -> 0 or -err            */
#define SYS_MEMSHARE 0x32 /* (size) -> id or -err                 */
#define SYS_ATTACH 0x33   /* (id, addr) -> addr or -err           */
#define SYS_MAPDEV 0x34   /* (phys, size) -> addr or -err         */
#define SYS_DETACH 0x35   /* (phys, size) -> addr or -err         */
#define SYS_QUERYDEV 0x36 /* (handle, out_buf, len) -> irq or -err*/

/* ---- Interrupts (0x40-0x4F) ---- */

#define SYS_IRQ_CLAIM 0x40 /* (irq_num) -> 0 or -err              */
#define SYS_IRQ_BIND 0x41  /* (irq_num) -> 0 or -err              */
#define SYS_IRQ_DONE 0x42  /* (irq_num) -> 0 or -err              */

/* ---- Experimental / debugging / temporary (0xF0-0xFF) ---- */

#define SYS_MAX 0xFF

/* ---- Error codes (returned as negative r0) ---- */

#define ERR_NOPERM (-1)   /* Operation not permitted              */
#define ERR_NOENT (-2)    /* Not found                            */
#define ERR_BUSY (-3)     /* Resource busy, try again             */
#define ERR_NOMEM (-4)    /* Out of memory                        */
#define ERR_BADFORM (-5)  /* Bad handle                           */
#define ERR_BADARG (-6)   /* Invalid argument                     */
#define ERR_NOMATCH (-7)  /* Syscall not implemented              */
#define ERR_PTRFAULT (-8) /* Bad pointer                          */
#define ERR_DEAD (-9)     // endpoint destroyed while waiting

#endif /* SYSCALL_NUMS_H */
