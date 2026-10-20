/**
 * @file err.h
 * @brief Names error types from the kernel and the userspace IPC contracts.
 */

#ifndef ZUZU_ERR_H
#define ZUZU_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief Generic error code type. Never positive */
typedef int32_t Err;

/** @brief No error has occurred, the operation was successful. */
#define ZUZU_OK 0

/** @brief Caller does not have the necessary permission for this operation. */
#define ERR_NOPERM (-1)

/** @brief The requested entry in a list, table etc. could not be found */
#define ERR_NOENT (-2)

/** @brief The requested resource is being used, is referenced, or currently unavailable. Try again
 * or ensure no other party is using it. */
#define ERR_BUSY (-3)

/** @brief Memory for this operation could not be allocated. */
#define ERR_NOMEM (-4)

/** @brief Type-check failed, this handle is not the required type. */
#define ERR_BADTYPE (-5)

/** @brief Argument is NULL or otherwise unusable. */
#define ERR_BADARG (-6)

/** @brief This operation is not supported by the system. */
#define ERR_NOSYS (-7)

/** @brief The pointer does not point to the required data, is NULL, or unfit. */
#define ERR_BADPTR (-8)

/** @brief Object owner has died. */
#define ERR_DEAD (-9)

/** @brief Wait timed out. */
#define ERR_TIMEOUT (-10)

/** @brief The transport request will not fit into the destination. */
#define ERR_OVERFLOW (-11)

/** @brief This handle is not a valid handle for this operation. */
#define ERR_BADHANDLE (-12)

/** @brief Buffer full, cannot receive anymore */
#define ERR_BUFFULL (-100)

/** @brief Buffer empty, cannot send anymore */
#define ERR_BUFEMPTY (-101)

/** @brief System is not up (yet). */
#define ERR_SYSDOWN (-102)

/** @brief System is not connected. */
#define ERR_NOTCONN (-103)

/** @brief This is a duplicate, and having duplicates in this operation would be invalid. */
#define ERR_DUPLICATE (-104)

/** @brief Format is incorrect. */
#define ERR_MALFORMED (-105)

/** @brief Input/output has failed. */
#define ERR_IO (-106)

/** @brief This request is outdated, or operation cannot continue due to an issue with outdated
 * code/data. */
#define ERR_OUTDATED (-107)

/** @brief TCP connection reset */
#define ERR_CONNRESET (-108)

/** @brief TCP connection refused. */
#define ERR_CONNREFUSED (-109)

/** @brief Task kill reason. */
typedef enum {
    KILLED_BY_PARENT,      /**< Parent destroyed the task object. */
    KILLED_FAULT_DATA,     /**< The task caused a data abort */
    KILLED_FAULT_PREFETCH, /**< The task caused a prefetch abort */
    KILLED_FAULT_UNDEF,    /**< The task executed an undefined instruction */
    KILLED_FAULT_ALIGN,    /**< The task accessed unaligned memory. */
    KILLED_OOM,            /**< The task was victim to an OOM service. */
} KilledReason;

/**
 * @brief Converts an error code into a short explanatory string, or ERR -XXX if the error is
 * unknown.
 *
 * @param[in] errcode Numeric error code.
 * @return Explanatory string.
 */
const char *StrToError(Err errcode);

#ifdef __cplusplus
}
#endif

#endif /*  ZUZU_ERR_H */
