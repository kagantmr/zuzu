/**
 * @file service.h
 * @brief Utilities to look up or register services to and from the name service.
 */

#ifndef ZUZU_SERVICE_H
#define ZUZU_SERVICE_H

#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The port to find the name service.
 * 
 * The name service must be at handle 0 at all times, and while it is not illegal for a service to not to,
 * the public port is granted around in the libraries and it is assumed to be at handle entry 0.
 */
#define NSVC_PORT 0

/**
 * @brief Maximum length of a name service path.
 */
#define NS_MAX_PATH 64

/**
 * @brief Name service opcode enum
 */
typedef enum {
    NS_REGISTER = 1, /**< This request belongs to a "register" request. */
    NS_LOOKUP /**< This request belongs to a "lookup" request. */
} NsvcOpcode;

/**
 * @brief Name service message wire format.
 */
typedef struct {
    NsvcOpcode cmd; /**< Request opcode. */
    Handle handle;  /**< If cmd == NS_REGISTER, then use this handle in the name table. */
    char *path; /**< If cmd == NS_REGISTER, then register under this name. If cmd == NS_LOOKUP, look up a service with this name. */
} NsvcRequest;

/**
 * @brief Registers a service using the given port handle with the given name
 * 
 * @param[in] name The path to register under, e.g. "/svc/example"
 * @param[in] port Handle to a port object the name server will associate with the path.
 * 
 * @return ZUZU_OK on success, negative error code on failure.
 * @retval ERR_OVERFLOW @p name was too long.
 * 
 */
Err RegisterService(const char *name, Handle port);

/**
 * @brief Find a service by its path.
 * 
 * @param[in] name The service path to look up.
 * @return The found port handle on success, or negative on failure. 
 */
Handle LookupService(const char *name);

#ifdef __cplusplus
}
#endif

#endif
