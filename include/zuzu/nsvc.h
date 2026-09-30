#ifndef ZUZU_NS_PROTOCOL_H
#define ZUZU_NS_PROTOCOL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <zuzu/err.h>
#include <types.h>

#define NS_MAX_PATH 64
#define NS_MAX_SERVICES 512

typedef enum
{
    NS_REGISTER = 1, /* register port into nt */
    NS_LOOKUP       /* Look up a name  */
} NsvcOpcode;

typedef struct
{
    NsvcOpcode cmd;
    Handle handle; /* NT_REGISTER: the granted slot. unused otherwise */
    Spid pid;       /* NT_LOOKUP_PID / NT_SCRUB_PID target */
    char *path;    /* points into the lmsg buf; unused for pid ops */
} NsvcRequest;

#ifdef __cplusplus
}
#endif

#endif /*  ZUZU_NT_PROTOCOL_H */