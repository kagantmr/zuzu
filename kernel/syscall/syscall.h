#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "arch/arm/include/context.h"
#include "stdbool.h"
#include "stddef.h"
#include "kernel/mm/vmm.h"
#include "stdint.h"
#include "zuzu/syscall_nums.h"

/*
 * zuzu Syscall ABI
 *
 * Syscall numbers encoded in the lower 8 bits of SVC immediate.
 * Arguments in r0-r3, return in r0. See docs/syscall.md for full ABI.
 */

void syscall_dispatch(uint8_t svc_num, exception_frame_t *frame);

static inline bool validate_user_ptr(const uintptr_t addr, const size_t len) {
    if (addr + len < addr) return false;
    if (addr >= USER_VA_TOP) return false;
    if (addr + len > USER_VA_TOP) return false;
    return true;
}

#endif /* KERNEL_SYSCALL_H */