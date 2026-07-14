#include "sys_dev.h"
#include "kernel/syscall/syscall.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/process.h"
#include <arch/mmu.h>

#include <zuzu/syscall_nums.h>
#include <string.h>
#include <zuzu/user_layout.h>
#include <mem.h>
#include <zuzu/types.h>

#define LOG_FMT(fmt) "(sys_dev) " fmt
#include "core/log.h"

extern thread_t *current_thread;

void sys_dev_query(arch_regs_t *frame) {
    handle_t handle_idx = (*arch_reg(frame, 0));
    char *out_buf = (char *)(*arch_reg(frame, 1));
    size_t buf_len = (*arch_reg(frame, 2));
    char compat_buf[sizeof(((device_cap_t *)0)->compatible) + 1];

    if (handle_idx == 0 || buf_len == 0) { (*arch_reg(frame, 0)) = ERR_BADARG; return; }

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle_idx);
    if (!entry) { (*arch_reg(frame, 0)) = ERR_BADHANDLE; return; }
    if (entry->type != HANDLE_DEVICE) { (*arch_reg(frame, 0)) = ERR_BADTYPE; return; }

    device_cap_t *cap = entry->dev;
    if (!cap) { (*arch_reg(frame, 0)) = ERR_BADHANDLE; return; }

    strncpy(compat_buf, cap->compatible, sizeof(compat_buf) - 1);
    compat_buf[sizeof(compat_buf) - 1] = '\0';

    size_t copy_len = strlen(compat_buf) + 1;
    if (copy_len > buf_len) {
        copy_len = buf_len;
        compat_buf[copy_len - 1] = '\0';
    }

    if (!copy_to_user(out_buf, compat_buf, copy_len)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    (*arch_reg(frame, 0)) = cap->irq;  // return irq in r0 as bonus
}
