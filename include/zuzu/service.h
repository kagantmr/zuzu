#ifndef ZUZU_SERVICE_H
#define ZUZU_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <zuzu/lmsg.h>
#include <stdint.h>
#include <zuzu/protocols/nt_protocol.h>

/**
 * @brief Registers a service with the specified name with sysd.
 * 
 * @param name The name of the service to register.
 * @return int32_t Returns the registered port on success, or a negative error code on failure.
 */
int32_t register_service(const char *name);

/**
 * @brief Looks up a service by name and returns its handle.
 * 
 * @param name The name of the service to look up.
 * @return int32_t Returns the handle of the service on success, or a negative error code on failure.
 */
int32_t lookup_service(const char *name);

#ifdef __cplusplus
}
#endif

#endif