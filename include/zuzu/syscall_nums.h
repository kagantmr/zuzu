#ifndef SYSCALL_NUMS_H
#define SYSCALL_NUMS_H

#include "err.h"

/* ---- Process/Thread lifecycle (0x00-0x0F) ---- */
#define SYS_PQUIT 0x00         /* (status) -> never returns */
#define SYS_YIELD 0x01         /* () -> 0 */
#define SYSCALL_RESERVED1 0x02 /* Reserved, used to be log() */
#define SYS_WAIT 0x03          /* (pid, &status) -> 0 or -err */
#define SYS_GETPID 0x04        /* () -> pid */
#define SYS_SLEEP 0x05         /* (duration_ms) -> 0, no infinite sleep or polling */
#define SYS_PSPAWN 0x06        /* (name) -> empty process (0) or -err */
#define SYS_KICKSTART 0x07     /* (args*) -> 0 or -err */
#define SYS_PKILL 0x08         /* (task_handle) -> 0 or -err */
#define SYS_TMAKE 0x09         /* (entry, user_sp, arg) -> tid */
#define SYS_TJOIN 0x0A         /* (tid) -> exit_status or -err */
#define SYS_TQUIT 0x0B         /* (status) -> never returns */

/* ---- Messaging (0x10-0x1F) ---- */
#define SYS_MSG_SEND 0x10   /* (port_handle, r1-r3) -> 0 or -err */
#define SYS_MSG_RECV 0x11   /* (port_handle) -> sender, r1-r3 payload */
#define SYS_MSG_CALL 0x12   /* (port_handle, r1-r3) -> r0-r3 reply */
#define SYS_MSG_REPLY 0x13  /* (r0-r3) -> 0 or -err */
#define SYS_MSG_LSEND 0x14  /* (port, len) -> 0 or -err; len <= 512 bytes, else ERR_OVERFLOW*/
#define SYS_MSG_LCALL 0x15  /* (port, len) -> r0=0, r1=recv_len (only success case)*/
#define SYS_MSG_LREPLY 0x16 /* (reply_handle, len) -> 0 or -err */
#define SYS_WAITANY 0x17    /* (handles*, count, timeout, result*) -> 0 or -err. Result struct's first field is caller-set size, 0=poll / UINT32_MAX=infinite, poll-empty returns ERR_TIMEOUT. */

/* ---- Handles/Capabilities (0x20-0x2F) ---- */
#define SYS_PORT_CREATE 0x20 /* () -> port_handle or -err */
#define SYS_NTFN_CREATE 0x21 /* () -> ntfn_handle or -err */
#define SYS_DEV_QUERY 0x22   /* (name/class, out*) -> handle or -err */
#define SYS_GRANT 0x23       /* (handle, pid) -> 0 or -err; any type */
#define SYS_DESTROY 0x24     /* (handle) -> 0 or -err; refuses REPLY/TASK, ERR_BUSY if mapped */
#define SYS_NTFN_SIGNAL 0x25 /* (ntfn_handle, bits) -> 0 or -err; bits are 31-bit, bit 31 rejected */
#define SYS_NTFN_WAIT 0x26   /* (ntfn_handle, timeout_ms) -> bits or -err; bit 31 reserved for errors */

/* ---- Memory (0x30-0x3F) ---- */
#define SYS_MEMMAP 0x30        /* (handle|HANDLE_ANON, size, prot, flags) -> va or -err. ANON: size>0, <=32MB. DEVICE/SHM: size must be 0. prot=R/W/X only, W^X enforced, EXEC rejected on device. flags must be 0. */
#define SYS_MEMUNMAP 0x31      /* (addr) -> 0 or -err */
#define SYS_SHM_CREATE 0x32    /* (size) -> handle or -err */
#define SYSCALL_RESERVED2 0x33 /* Reserved, used to be shm_attach() */
#define SYSCALL_RESERVED3 0x34 /* Reserved, used to be mapdev() */
#define SYSCALL_RESERVED4 0x35 /* Reserved, used to be shm_detach() */
#define SYSCALL_RESERVED5 0x36 /* Reserved, used to be querydev( )*/
#define SYS_MEMPROTECT 0x37    /* (addr, size, prot) -> 0 or -err. Refuses PINNED/GUARD regions, rejects EXEC on MMIO, won't span regions. */
#define SYS_ASINJECT 0x38      /* (args*) -> 0 or -err; args->size first field */

/* ---- Interrupts (0x40-0x4F) ---- */
/* 0x40 (irq_claim) retired: folded into SYS_IRQ_BIND */
#define SYSCALL_RESERVED6 0x40 /* Reserved, used to be irq_claim */
#define SYS_IRQ_BIND 0x41      /* (dev_handle, ntfn_handle) -> 0 or -err. */
#define SYS_IRQ_DONE 0x42      /* (dev_handle) -> 0 or -err. */

/* ---- Experimental / debugging / temporary (0xF0-0xFF) ---- */

#define SYS_MAX 0xFF

#endif /* SYSCALL_NUMS_H */
