#ifndef ZUZU_NT_PROTOCOL_H
#define ZUZU_NT_PROTOCOL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <zuzu/err.h>
#include <zuzu/types.h>

#define NT_PORT 0
#define NT_PID 3
#define NT_MAX_PATH 64
#define NT_MAX_SERVICES 512

    typedef enum
    {
        NT_REGISTER = 1, /* register port into nt */
        NT_LOOKUP,       /* Look up a name  */
        NT_LOOKUP_PID,   /* Look up a pid (used for tty aliasing, change later)*/
        NT_SCRUB_PID     /* sysd telling name server that a process died */
    } NtOpcode;

    typedef struct
    {
        NtOpcode cmd;
        Handle handle; /* NT_REGISTER: the granted slot. unused otherwise */
        Pid pid;       /* NT_LOOKUP_PID / NT_SCRUB_PID target */
        char *path;    /* points into the lmsg buf; unused for pid ops */
    } NtRequest;

#ifdef __cplusplus
}
#endif

#endif /*  ZUZU_NT_PROTOCOL_H */
