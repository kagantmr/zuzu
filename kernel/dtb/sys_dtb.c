#include "sys_dtb.h"
#include "dtb.h"
#include "kernel/syscall/syscall.h"
#include <string.h>
#include <mem.h>

#define LOG_FMT(fmt) "(sys_dtb) " fmt
#include "core/log.h"

#define DTB_KBUF_MAX 128

static size_t copy_string_from_user(uintptr_t uptr, char *kbuf, size_t kbuf_cap)
{
    if (!kbuf_cap)
        return 0;

    /* validate the maximum range we'll touch */
    if (!validate_user_ptr(uptr, kbuf_cap))
        return 0;

    const char *src = (const char *)uptr;
    size_t len = strnlen(src, kbuf_cap - 1);
    memcpy(kbuf, src, len);
    kbuf[len] = '\0';
    return len;
}

void dtb_find(exception_frame_t *frame)
{
    uintptr_t u_compat   = frame->r[0];
    uintptr_t u_path_out = frame->r[1];
    uint32_t  path_cap   = frame->r[2];

    char kcompat[DTB_KBUF_MAX];
    char kpath[DTB_KBUF_MAX];

    /* copy compatible string from user */
    size_t clen = copy_string_from_user(u_compat, kcompat, sizeof(kcompat));
    if (clen == 0) {
        frame->r[0] = ERR_PTRFAULT;
        return;
    }

    /* validate output buffer */
    if (path_cap == 0 || !validate_user_ptr(u_path_out, path_cap)) {
        frame->r[0] = ERR_PTRFAULT;
        return;
    }

    /* query the DTB */
    if (!dtb_find_compatible(kcompat, kpath, sizeof(kpath))) {
        frame->r[0] = ERR_NOENT;
        return;
    }

    /* copy result to user */
    size_t plen = strlen(kpath);
    size_t copy_len = plen < path_cap - 1 ? plen : path_cap - 1;
    memcpy((void *)u_path_out, kpath, copy_len);
    ((char *)u_path_out)[copy_len] = '\0';

    frame->r[0] = (uint32_t)plen;
}

void dtb_prop(exception_frame_t *frame)
{
    uintptr_t u_path    = frame->r[0];
    uintptr_t u_prop    = frame->r[1];
    uintptr_t u_buf_out = frame->r[2];
    uint32_t  buf_cap   = frame->r[3];

    char kpath[DTB_KBUF_MAX];
    char kprop[DTB_KBUF_MAX];

    /* copy path from user */
    size_t plen = copy_string_from_user(u_path, kpath, sizeof(kpath));
    if (plen == 0) {
        frame->r[0] = ERR_PTRFAULT;
        return;
    }

    /* copy property name from user */
    size_t nlen = copy_string_from_user(u_prop, kprop, sizeof(kprop));
    if (nlen == 0) {
        frame->r[0] = ERR_PTRFAULT;
        return;
    }

    /* validate output buffer */
    if (buf_cap == 0 || !validate_user_ptr(u_buf_out, buf_cap)) {
        frame->r[0] = ERR_PTRFAULT;
        return;
    }

    /* query the DTB */
    const void *val = NULL;
    uint32_t val_len = 0;
    if (!dtb_get_property(kpath, kprop, &val, &val_len)) {
        frame->r[0] = ERR_NOENT;
        return;
    }

    /* copy result to user */
    uint32_t copy_len = val_len < buf_cap ? val_len : buf_cap;
    memcpy((void *)u_buf_out, val, copy_len);

    frame->r[0] = (uint32_t)val_len;
}

void dtb_reg(exception_frame_t *frame)
{
    uintptr_t u_path = frame->r[0];
    uint32_t index = frame->r[1];

    char kpath[DTB_KBUF_MAX];
    size_t plen = copy_string_from_user(u_path, kpath, sizeof(kpath));
    if (plen == 0) {
        frame->r[0] = ERR_PTRFAULT;
        return;
    }

    uint64_t addr = 0;
    uint64_t size = 0;
    if (!dtb_get_reg_phys(kpath, (int)index, &addr, &size)) {
        frame->r[0] = ERR_NOENT;
        return;
    }

    if (addr > 0xFFFFFFFFULL || size > 0xFFFFFFFFULL) {
        frame->r[0] = ERR_BADFORM;
        return;
    }

    frame->r[0] = 0;
    frame->r[1] = (uint32_t)addr;
    frame->r[2] = (uint32_t)size;
    frame->r[3] = 0;
}
