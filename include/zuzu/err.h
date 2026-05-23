#ifndef ZUZU_ERR_H
#define ZUZU_ERR_H

#include <stdint.h>

typedef int32_t err_t;

#define ZUZU_OK 0

#define ERR_NOPERM (-1)    /* Operation not permitted              */
#define ERR_NOENT (-2)     /* Not found                            */
#define ERR_BUSY (-3)      /* Resource busy, try again             */
#define ERR_NOMEM (-4)     /* Out of memory                        */
#define ERR_MALFORMED (-5)   /* Bad handle                           */
#define ERR_BADARG (-6)    /* Invalid argument                     */
#define ERR_NOMATCH (-7)   /* Syscall not implemented              */
#define ERR_BADPTR (-8)    /* Bad pointer                          */
#define ERR_DEAD (-9)      /* Endpoint destroyed while waiting     */
#define ERR_TIMEOUT (-10)  /* IPC or operation timed out           */
#define ERR_OVERFLOW (-11) /* Buffer overflow or out of range      */
#define ERR_BUFFULL (-12)  /* Buffer full (e.g. channel send)      */
#define ERR_SYSDOWN (-13)  /* System service is down/unavailable   */

const char *strerror(err_t err);

#endif // ZUZU_ERR_H