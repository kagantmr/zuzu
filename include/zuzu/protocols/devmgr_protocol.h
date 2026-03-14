#ifndef DEVMGR_PROTOCOL_H
#define DEVMGR_PROTOCOL_H

/*
 * DEV_REQUEST  r2=class -> r1=0 ok, r2=dev_handle
 * Caller does _irq_claim/_irq_bind/_irq_done directly after receiving dev_handle.
 */
#define DEV_REQUEST 1

/*
 * DEV_REGISTER r2=class, r3=granted shmem handle containing NUL-terminated compatible string
 * Reply r1=DEV_REG_OK or negative error.
 */
#define DEV_REGISTER 2

#define DEV_REG_OK   0
#define DEV_REG_FULL (-3)
#define DEV_REG_DUP  (-5)

/* Device classes - clients request by class, devmgr resolves to a concrete device */
#define DEV_CLASS_SERIAL 1 /* first UART / serial console */
#define DEV_CLASS_RTC    2 /* real-time clock */
#define DEV_CLASS_BLOCK  3 /* first block storage device */

#define ERR_BADCMD (-1)
#define ERR_NOENT  (-2)
/* ERR_NOMEM is defined as (-4) in <zuzu/syscall_nums.h> */

#endif
