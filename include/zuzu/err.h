#ifndef ZUZU_ERR_H
#define ZUZU_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef int32_t Err;

#define ZUZU_OK 0

#define ERR_NOPERM (-1)
#define ERR_NOENT (-2)
#define ERR_BUSY (-3)
#define ERR_NOMEM (-4)
#define ERR_BADTYPE (-5)
#define ERR_BADARG (-6)
#define ERR_NOSYS (-7)
#define ERR_BADPTR (-8)
#define ERR_DEAD (-9)
#define ERR_TIMEOUT (-10)
#define ERR_OVERFLOW (-11)
#define ERR_BADHANDLE (-12)

#define ERR_BUFFULL (-100)
#define ERR_BUFEMPTY (-101)
#define ERR_SYSDOWN (-102)
#define ERR_NOTCONN (-103)
#define ERR_DUPLICATE (-104)
#define ERR_MALFORMED (-105)
#define ERR_IO (-106)
#define ERR_OUTDATED                                                                               \
    (-107)
#define ERR_CONNRESET (-108)
#define ERR_CONNREFUSED (-109)

#define FATAL_TAG 0xFA7A0000u
#define FATAL_TAG_MASK 0xFFFF0000u
#define FATAL_REASON_MASK 0x0000FFFFu
#define FATAL_KERNEL_OUTDATED 1
#define FATAL_KERNEL_REFERLMSG 2

typedef enum {
    KILLED_BY_PARENT,
    KILLED_FAULT_DATA,
    KILLED_FAULT_PREFETCH,
    KILLED_FAULT_UNDEF,
    KILLED_FAULT_ALIGN,
    KILLED_OOM,
} KilledReason;

const char *StrToError(Err errcode);

#ifdef __cplusplus
}
#endif

#endif // ZUZU_ERR_H
