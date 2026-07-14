#include "exec.h"
#include <zuzu/zuzu.h>
#include <mem.h>
#include <string.h>
#include <malloc.h>
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
        int32_t rc = zuzu_asinject(task_handle, ph->p_vaddr,
                               (const uint8_t *)elf_data + ph->p_offset,
                               ph->p_filesz, prot);
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

        void *zeroes = malloc(bss_len);
        if (!zeroes) return -1;
        memset(zeroes, 0, bss_len);

        int32_t rc = zuzu_asinject(task_handle, bss_start, zeroes, bss_len,
                               VM_PROT_READ | VM_PROT_WRITE);
        free(zeroes);
        if (rc != 0) return rc;
    }

    return 0;
}

static int inject_stack(uint32_t task_handle,
                        const char *argbuf, size_t argbuf_len,
                        uint32_t argc,
                        uintptr_t *out_sp, uintptr_t *out_argv)
{
    /* The kernel reserves the full demand-paged stack window; we only
     * inject an initial image at the top of it holding argv. */
    const uintptr_t img_base = USER_STACK_TOP - USER_STACK_SIZE;

    uint8_t *buf = malloc(USER_STACK_SIZE);
    if (!buf) return -1;
    memset(buf, 0, USER_STACK_SIZE);

    uintptr_t sp = USER_STACK_TOP;
    uintptr_t argv_va = 0;

    if (argbuf && argbuf_len > 0 && argc > 0) {
        sp -= argbuf_len;
        sp &= ~3u;
        uintptr_t strings_va = sp;

        // copy string data into the local stack buffer
        size_t buf_off = strings_va - img_base;
        if (buf_off > USER_STACK_SIZE || argbuf_len > USER_STACK_SIZE - buf_off) {
            free(buf);
            return -1;
        }
        memcpy(buf + buf_off, argbuf, argbuf_len);

        // build argv pointer array
        sp -= (argc + 1) * sizeof(uint32_t);
        sp &= ~7u;
        argv_va = sp;

        size_t argv_off = (size_t)(argv_va - img_base);
        size_t argv_bytes = (argc + 1) * sizeof(uint32_t);
        if (argv_off > USER_STACK_SIZE || argv_bytes > USER_STACK_SIZE - argv_off) {
            free(buf);
            return -1;
        }

        uint32_t *argv_arr = (uint32_t *)(buf + argv_off);
        uintptr_t str_va = strings_va;
        for (uint32_t a = 0; a < argc; a++) {
            argv_arr[a] = (uint32_t)str_va;
            // walk past this string's NUL
            size_t soff = str_va - img_base;
            str_va += strlen((const char *)(buf + soff)) + 1;
        }
        argv_arr[argc] = 0;
    }

    int32_t rc = zuzu_asinject(task_handle, img_base, buf, USER_STACK_SIZE,
                           VM_PROT_READ | VM_PROT_WRITE);
    free(buf);
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
    out->pid = 0; 
    return rc;
}