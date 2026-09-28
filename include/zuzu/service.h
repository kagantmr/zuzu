#ifndef ZUZU_SERVICE_H
#define ZUZU_SERVICE_H

#include "types.h"
#ifdef __cplusplus
extern "C"
{
#endif

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
    Spid pid;       /* NT_LOOKUP_PID / NT_SCRUB_PID target */
    char *path;    /* points into the lmsg buf; unused for pid ops */
} NtRequest;

/**
    * @brief Registers a service with the specified name with sysd.
    *
    * @param name The name of the service to register.
    * @param port The port to register.
    * @return Handle Returns the registered port on success, or a negative error code on failure.
    */
Err RegisterService(const char *name, Handle port);

/**
    * @brief Looks up a service by name and returns its handle.
    *
    * @param name The name of the service to look up.
    *
    * @return Handle Returns the handle of the granted port to the service on success, or a
    * negative error code on failure.
    */
Handle LookupService(const char *name);


#ifdef __cplusplus
}
#endif

#endif
