/** cap.h -- zuzu kernel port and capability syscalls
 *
 * Wraps the raw syscalls over a clean API.
 */

#ifndef ZUZU_CAP_H
#define ZUZU_CAP_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <arch/syscall.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a new port and returns its handle.
 *
 * @return Err Returns the handle of the newly created port on success, or a negative error code on failure.
 */
static inline Err ZuzuPortCreate(void) {
    return Syscall(SYS_PORT_CREATE, 0, 0, 0, 0);
}

#define GRANT_REGRANTABLE (1u << 0)

/**
 * @brief Grants a capability to the specified process.
 *
 * @param cap The handle of the capability to grant.
 * @param pid The process ID of the target process to grant the capability to.
 * @param flags Flags to set (REGRANTABLE)
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuGrant(Handle cap, Pid pid, uint32_t flags) {
    return Syscall(SYS_GRANT, cap, pid, flags, 0);
}

/**
 * @brief Destroys the specified handle, revoking its capability.
 *
 * @param h The handle to destroy.
 *
 * @return Handle Returns handle slot on the grantee's  on success, or a negative error code on failure.
 */
static inline Handle ZuzuDestroy(Handle h) {
    return Syscall(SYS_DESTROY, h, 0, 0, 0);
}

/**
 * @brief Stamps (badges) an endpoint handle, minting a new handle to the same
 * endpoint that carries the given marker. The source handle is left
 * untouched (non-consuming) and can be stamped again with a different
 * marker to mint additional badges. A handle that is already marked cannot
 * itself be re-stamped.
 *
 * @param handle The endpoint handle to stamp. Must be unmarked (marker == MARKER_NONE).
 * @param marker The marker to stamp with. Must be nonzero (0 is the reserved unmarked sentinel).
 *
 * @return Err Returns the new (marked) handle on success, or a negative error code on failure.
 */
static inline Err ZuzuStamp(Handle handle, Marker marker) {
    return Syscall(SYS_STAMP, handle, marker, 0, 0);
}

/**
 * @brief Labels a process with an 32-bit label (sysd only)
 *
 * @param handle The handle to the task to assign the label to.
 * @param marker The The value to assign to the label
 *
 * @return Err 0 or negative error code.
 */
static inline Err ZuzuSetLabel(Handle task_handle, Label label) {
    return Syscall(SYS_SET_LABEL, task_handle, label, 0, 0);
}

#ifdef __cplusplus
}
#endif

#endif
