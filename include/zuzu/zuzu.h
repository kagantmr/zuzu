#ifndef ZUZU_H
#define ZUZU_H

/* 
 * zuzu.h - Umbrella header for Zuzu user ABI
 * 
 * Includes all user-space syscall interfaces, organized by functionality:
 * - types.h:  common types (msg_t, tspawn_result_t, etc)
 * - task.h:   process lifecycle management
 * - ipc.h:    inter-process communication and ports
 * - umem.h:   memory management syscalls
 * - irq.h:    IRQ handling and claiming
 * - ntfn.h:   notification syscalls
 */

#include "zuzu/types.h"
#include "zuzu/task.h"
#include "zuzu/msg.h"
#include "zuzu/umem.h"
#include "zuzu/irq.h"
#include "zuzu/ntfn.h"

#endif
