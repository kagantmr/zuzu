#ifndef DEVMGR_PROTOCOL_H
#define DEVMGR_PROTOCOL_H

/*
 * DEV_REQUEST (call)
 *   req:  w1=DEV_REQUEST, w2=class
 *   resp: r1=0 ok, r2=dev_handle
 */
#define DEV_REQUEST 1

/*
 * DEV_REGISTER (call)
 *   req:  w1=DEV_REGISTER, w2=class
 *   resp: r1=DEV_REG_OK or negative error
 *
 * devmgr binds class -> one injected device capability owned by devmgr.
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
