#ifndef DEVMGR_PROTOCOL_H
#define DEVMGR_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DEV_REQUEST (call)
 *   req:  w1=DEV_REQUEST, w2=class
 *   resp: r1=0 ok, r2=dev_handle
 */
#define DEV_REQUEST 1

/*
 * DEV_REGISTER (call)
 *   req:  w1=DEV_REGISTER, w2=class
 *   resp: r1=ZUZU_OK or negative err_t (see <zuzu/err.h>)
 *
 * devmgr binds class -> one injected device capability owned by devmgr.
 */
#define DEV_REGISTER 2

/* Device classes - clients request by class, devmgr resolves to a concrete device */
#define DEV_CLASS_SERIAL 1 /* first UART / serial console */
#define DEV_CLASS_RTC    2 /* real-time clock */
#define DEV_CLASS_BLOCK  3 /* first block storage device */
#define DEV_CLASS_NIC    4 /* first network interface controller */

/* Error codes (ERR_NOENT, ERR_NOSYS, ERR_NOMEM, ...) live in <zuzu/err.h> */

#ifdef __cplusplus
}
#endif

#endif
