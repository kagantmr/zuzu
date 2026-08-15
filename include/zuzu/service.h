#ifndef ZUZU_SERVICE_H
#define ZUZU_SERVICE_H

#include "zuzu/types.h"
#ifdef __cplusplus
extern "C"
{
#endif

#include <zuzu/lmsg.h>
#include <zuzu/protocols/nametable.h>

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

    /**
     * @brief Inverse lookup: Looks up a service by PID, gets its port. (sysd only)
     *
     * @param Pid The PID of the service to look up
     *
     * @return Handle Return the handle of the granted port to the service on success, or
     * a negative error code on failure.
     **/
    Handle LookupServicePid(Pid pid);

    /**
     * @brief On process death, remove an entry from the nametable. (sysd only)
     *
     * @param pid PID of the process to clean up
     *
     * @return Err ZUZU_OK on success, ERR_* on fail
     *
     **/
    Err ScrubServicePid(Pid pid);

    /**
     * @brief Looks up a service by name and returns its handle and PID.
     *
     * @param name The name of the service to look up.
     * @param out_pid The PID as an out-param.
     *
     * @return Handle Returns the handle of the granted port to the service on success, or a
     * negative error code on failure.
     */
    Handle LookupServiceWithPid(const char *name, Pid *out_pid);

#ifdef __cplusplus
}
#endif

#endif
