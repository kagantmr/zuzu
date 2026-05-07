#ifndef ZUZU_ERR_H
#define ZUZU_ERR_H


#include <stdint.h>

typedef int32_t zuzu_err_t;

#define ZUZU_OK 0

#define ZUZU_ERR_NOPERM (-1)   /* Operation not permitted              */
#define ZUZU_ERR_NOENT (-2)    /* Not found                            */
#define ZUZU_ERR_BUSY (-3)     /* Resource busy, try again             */
#define ZUZU_ERR_NOMEM (-4)    /* Out of memory                        */
#define ZUZU_ERR_BADFORM (-5)  /* Bad handle                           */
#define ZUZU_ERR_BADARG (-6)   /* Invalid argument                     */
#define ZUZU_ERR_NOMATCH (-7)  /* Syscall not implemented              */
#define ZUZU_ERR_PTRFAULT (-8) /* Bad pointer                          */
#define ZUZU_ERR_DEAD (-9)     /* Endpoint destroyed while waiting     */
#define ZUZU_ERR_TIMEOUT (-10) /* IPC or operation timed out           */

const char *zuzu_strerror(zuzu_err_t err);

#endif // ZUZU_ERR_H