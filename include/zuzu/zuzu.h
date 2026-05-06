#ifndef ZUZU_H
#define ZUZU_H

/* 
 * zuzu.h - Umbrella header for Zuzu user ABI
 * 
 * Includes all user-space syscall interfaces, organized by functionality:
 * - task.h:  process lifecycle management
 * - ipc.h:   inter-process communication and ports
 * - mem.h:   memory management syscalls
 * - irq.h:   IRQ handling and claiming
 * - ntfn.h:  notification syscalls
 */

#include "zuzu/task.h"
#include "zuzu/ipc.h"
#include "zuzu/umem.h"
#include "zuzu/irq.h"
#include "zuzu/ntfn.h"

#endif
