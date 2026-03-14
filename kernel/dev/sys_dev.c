#include "sys_dev.h"
#include <mem.h>
#include "kernel/syscall/syscall.h"
#include "kernel/mm/alloc.h"
#include <zuzu/syscall_nums.h>
#include <zuzu/dev_enum.h>
#include "kernel/dtb/dtb.h"
#include "kernel/proc/process.h"

extern process_t *current_process;

typedef struct {
    zuzu_devinfo_t *buf;
    uint32_t start_index;
    uint32_t max_records;
    uint32_t seen;
    uint32_t written;
} enumdev_state_t;

static enumdev_state_t g_enumdev_state;

static void enumdev_collect_cb(const char *compatible,
                               uint64_t phys, uint64_t size,
                               uint32_t irq)
{
    enumdev_state_t *state = &g_enumdev_state;

    if (state->seen < state->start_index)
    {
        state->seen++;
        return;
    }

    if (state->written < state->max_records)
    {
        zuzu_devinfo_t *out = &state->buf[state->written];
        out->id = state->seen;
        out->phys_base = (uint32_t)phys;
        out->size = (uint32_t)size;
        out->irq = irq;

        size_t i = 0;
        while (compatible[i] != '\0' && i < sizeof(out->compatible) - 1)
        {
            out->compatible[i] = compatible[i];
            i++;
        }
        out->compatible[i] = '\0';
    }

    if (state->written < state->max_records)
    {
        state->written++;
    }
    state->seen++;
}

void getdev(exception_frame_t *frame) {
    const char *string = (const char *)frame->r[0];
    size_t len = (size_t)frame->r[1];

    if (!(current_process->flags & PROC_FLAG_HW_ACCESS)) {
        frame->r[0] = ERR_NOPERM;
        return;
    }
    if (!validate_user_ptr((uintptr_t)string, len) || len == 0 || len >= 64) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    char compatible[64];
    memmove(compatible, string, len);
    compatible[len] = '\0';

    char path[128];
    if (!dtb_find_compatible(compatible, path, sizeof(path))) {
        frame->r[0] = ERR_NOENT;
        return;
    }

    uint64_t phys, size;
    if (!dtb_get_reg_phys(path, 0, &phys, &size)) {
        frame->r[0] = ERR_NOENT;
        return;
    }

    // allocate device_cap_t
    device_cap_t *cap = kmalloc(sizeof(device_cap_t));
    if (!cap) { frame->r[0] = ERR_NOMEM; return; }
    cap->phys_base = (uint32_t)phys;
    cap->size      = (uint32_t)size;
    cap->mapped    = false;
    memmove(cap->compatible, compatible, len + 1);
    uint32_t irq = 0, irq_flags = 0;
    dtb_get_irq(path, 0, &irq, &irq_flags);
    cap->irq = irq;

    // find free handle slot
    int slot = -1;
    for (int i = 1; i < MAX_HANDLE_TABLE; i++) {
        if (current_process->handle_table[i].type == HANDLE_FREE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) { kfree(cap); frame->r[0] = ERR_NOMEM; return; }

    current_process->handle_table[slot].type     = HANDLE_DEVICE;
    current_process->handle_table[slot].dev      = cap;
    current_process->handle_table[slot].grantable = true;
    current_process->handle_table[slot].mapped_va = 0;

    frame->r[0] = (uint32_t)slot;
}

void enumdev(exception_frame_t *frame)
{
    zuzu_devinfo_t *out_buf = (zuzu_devinfo_t *)frame->r[0];
    uint32_t max_records = frame->r[1];
    uint32_t start_index = frame->r[2];

    if (!(current_process->flags & PROC_FLAG_HW_ACCESS)) {
        frame->r[0] = ERR_NOPERM;
        return;
    }

    if (max_records == 0) {
        frame->r[0] = 0;
        frame->r[1] = ZUZU_ENUMDEV_DONE;
        return;
    }

    size_t bytes = (size_t)max_records * sizeof(zuzu_devinfo_t);
    if (!validate_user_ptr((uintptr_t)out_buf, bytes)) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    g_enumdev_state.buf = out_buf;
    g_enumdev_state.start_index = start_index;
    g_enumdev_state.max_records = max_records;
    g_enumdev_state.seen = 0;
    g_enumdev_state.written = 0;

    dtb_enum_devices(enumdev_collect_cb);

    frame->r[0] = (int32_t)g_enumdev_state.written;
    if (g_enumdev_state.seen > (start_index + g_enumdev_state.written)) {
        frame->r[1] = start_index + g_enumdev_state.written;
    } else {
        frame->r[1] = ZUZU_ENUMDEV_DONE;
    }
}

void mapdev(exception_frame_t *frame)
{
    uint32_t handle_idx = frame->r[0];

    if (handle_idx == 0 || handle_idx >= MAX_HANDLE_TABLE) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    handle_entry_t *entry = &current_process->handle_table[handle_idx];
    if (entry->type != HANDLE_DEVICE) {
        frame->r[0] = ERR_BADFORM;
        return;
    }

    device_cap_t *cap = entry->dev;
    if (!cap) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    if (cap->mapped) {
        frame->r[0] = ERR_BUSY;
        return;
    }

    uint32_t size_aligned = align_up(cap->size, 4096);
    uint32_t user_va = current_process->device_va_next;

    if (!vmm_map_range(current_process->as, user_va, cap->phys_base, size_aligned,
                       VM_PROT_READ | VM_PROT_WRITE | VM_PROT_USER,
                       VM_MEM_DEVICE, VM_OWNER_NONE, VM_FLAG_NONE))
    {
        frame->r[0] = ERR_NOMEM;
        return;
    }

    current_process->device_va_next += size_aligned;
    cap->mapped = true;
    entry->mapped_va = user_va;

    frame->r[0] = user_va;
}