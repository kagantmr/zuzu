#ifndef ZUZU_H
#define ZUZU_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * zuzu.h - Umbrella header for Zuzu user ABI
 *
 * Includes all user-space syscall interfaces, organized by functionality:
 * - types.h:  common types (Message, TSpawnResult, etc)
 * - task.h:   process lifecycle management
 * - ipc.h:    inter-process communication and ports
 * - cap.h:    port creation and capability management (grant/destroy/stamp/setlabel)
 * - umem.h:   memory management syscalls
 * - irq.h:    IRQ handling and claiming
 * - ntfn.h:   notification syscalls
 * - event.h:  kernel event syscalls
 */

#include "zuzu/types.h"
#include "zuzu/task.h"
#include "zuzu/msg.h"
#include "zuzu/cap.h"
#include "zuzu/umem.h"
#include "zuzu/irq.h"
#include "zuzu/ntfn.h"
#include "zuzu/event.h"

#ifdef __cplusplus
}
#endif

#endif
