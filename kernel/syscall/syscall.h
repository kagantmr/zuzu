#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <arch/regs.h>
#include "kernel/proc/thread.h"
#include "stdbool.h"
#include "stddef.h"
#include "kernel/mm/vmm.h"
#include "stdint.h"
#include "zuzu/syscall_nums.h"

/*
 * zuzu Syscall ABI (ARMv7-A)
 *
 * Syscall numbers encoded in the lower 8 bits of SVC immediate.
 * Arguments in r0-w3, return in r0. See docs/syscall.md for full ABI.
 */

typedef uint8_t Svc;

bool CopyToUser(void *uaddr, const void *kaddr, size_t len);
bool CopyFromUser(void *kaddr, const void *uaddr, size_t len);

void __attribute__((hot)) SyscallDispatch(Svc svc_num, CpuState *frame);

static inline bool validate_user_ptr(const uintptr_t addr, const size_t len) {
    if (addr + len < addr) return false;
    if (addr >= USER_VA_TOP) return false;
    if (addr + len > USER_VA_TOP) return false;
    return true;
}

extern thread_t *current_thread;

#endif /* KERNEL_SYSCALL_H */