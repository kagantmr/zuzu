#include "exec.h"
#include <zuzu.h>
#include <mem.h>
#include <string.h>
#include <zmalloc.h>
#include <elf.h>  // now a header-only or shared header
#include <zuzu/memprot.h>
#include <zuzu/user_layout.h>

static int inject_segment(uint32_t task_handle, const void *elf_data,
                          size_t elf_size, Elf32_Phdr *ph)
{
    if (ph->p_offset + ph->p_filesz > elf_size)
        return -1;

    uint32_t prot = 0;
    if (ph->p_flags & PF_R) prot |= VM_PROT_READ;
    if (ph->p_flags & PF_W) prot |= VM_PROT_WRITE;
    if (ph->p_flags & PF_X) prot |= VM_PROT_EXEC;

    // inject file-backed portion
    if (ph->p_filesz > 0) {
        asinject_args_t args = {
            .task_handle = task_handle,
            .dst_va      = ph->p_vaddr,
            .src_buf     = (const uint8_t *)elf_data + ph->p_offset,
            .len         = ph->p_filesz,
            .prot        = prot,
        };
        int32_t rc = _asinject(&args);
        if (rc != 0) return rc;
    }

    // BSS: memsz > filesz means zero-filled pages beyond the file data.
    // asinject already zeroes partial page tails, so we only need to handle
    // additional whole pages.
    uint32_t file_end = ph->p_vaddr + ph->p_filesz;
    uint32_t mem_end  = ph->p_vaddr + ph->p_memsz;
    uint32_t bss_start = (file_end + 0xFFF) & ~0xFFF;  // next page boundary

    if (bss_start < mem_end) {
        size_t bss_len = mem_end - bss_start;
        bss_len = (bss_len + 0xFFF) & ~0xFFF;  // round up to page

        void *zeroes = zmalloc(bss_len);
        if (!zeroes) return -1;
        memset(zeroes, 0, bss_len);

        asinject_args_t bss_args = {
            .task_handle = task_handle,
            .dst_va      = bss_start,
            .src_buf     = zeroes,
            .len         = bss_len,
            .prot        = VM_PROT_READ | VM_PROT_WRITE,
        };
        int32_t rc = _asinject(&bss_args);
        zfree(zeroes);
        if (rc != 0) return rc;
    }

    return 0;
}

static int inject_stack(uint32_t task_handle,
                        const char *argbuf, size_t argbuf_len,
                        uint32_t argc,
                        uintptr_t *out_sp, uintptr_t *out_argv)
{
    uint8_t *buf = zmalloc(USER_STACK_SIZE);
    if (!buf) return -1;
    memset(buf, 0, USER_STACK_SIZE);

    uintptr_t sp = USER_STACK_TOP;
    uintptr_t argv_va = 0;

    if (argbuf && argbuf_len > 0 && argc > 0) {
        sp -= argbuf_len;
        sp &= ~3u;
        uintptr_t strings_va = sp;

        // copy string data into the local stack buffer
        size_t buf_off = strings_va - USER_STACK_BASE;
        memcpy(buf + buf_off, argbuf, argbuf_len);

        // build argv pointer array
        sp -= (argc + 1) * sizeof(uint32_t);
        sp &= ~7u;
        argv_va = sp;

        uint32_t *argv_arr = (uint32_t *)(buf + (argv_va - USER_STACK_BASE));
        uintptr_t str_va = strings_va;
        for (uint32_t a = 0; a < argc; a++) {
            argv_arr[a] = (uint32_t)str_va;
            // walk past this string's NUL
            size_t soff = str_va - USER_STACK_BASE;
            str_va += strlen((const char *)(buf + soff)) + 1;
        }
        argv_arr[argc] = 0;
    }

    asinject_args_t args = {
        .task_handle = task_handle,
        .dst_va      = USER_STACK_BASE,
        .src_buf     = buf,
        .len         = USER_STACK_SIZE,
        .prot        = VM_PROT_READ | VM_PROT_WRITE,
    };
    int32_t rc = _asinject(&args);
    zfree(buf);
    if (rc != 0) return rc;

    *out_sp = sp;
    *out_argv = argv_va;
    return 0;
}

int exec_inject(uint32_t task_handle, const void *elf_data, size_t elf_size,
              const char *argbuf, size_t argbuf_len, uint32_t argc, exec_reply_t *out)
{
    uint32_t entry = elf_validate(elf_data, elf_size);
    if (!entry) return -1;

    int phdr_count = elf_phdr_count(elf_data);

    // check for overlapping segments
    for (int i = 0; i < phdr_count; i++) {
        Elf32_Phdr *a = elf_phdr_get(elf_data, i);
        if (a->p_type != PT_LOAD) continue;
        for (int j = i + 1; j < phdr_count; j++) {
            Elf32_Phdr *b = elf_phdr_get(elf_data, j);
            if (b->p_type != PT_LOAD) continue;
            uint32_t a_end = a->p_vaddr + a->p_memsz;
            uint32_t b_end = b->p_vaddr + b->p_memsz;
            if (a->p_vaddr < b_end && b->p_vaddr < a_end)
                return -1;
        }
    }

    // inject each PT_LOAD segment
    for (int i = 0; i < phdr_count; i++) {
        Elf32_Phdr *ph = elf_phdr_get(elf_data, i);
        if (ph->p_type != PT_LOAD) continue;
        int rc = inject_segment(task_handle, elf_data, elf_size, ph);
        if (rc != 0) return rc;
    }

    // inject user stack with argv
    uintptr_t sp = USER_STACK_TOP;
    uintptr_t argv_va = 0;
    int rc = inject_stack(task_handle, argbuf, argbuf_len, argc, &sp, &argv_va);
    if (rc != 0) return rc;

    /*
    kickstart_args_t ks = {
        .task_handle = task_handle,
        .entry       = entry,
        .sp          = sp,
        .r0_val      = argc,
        .r1_val      = (uint32_t)argv_va,
    };*/
    out->entry = entry;
    out->sp = sp;
    out->argc = argc;
    out->argv_va = argv_va;
    return rc;
}