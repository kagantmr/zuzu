#include "exec.h"
#include <zuzu/zuzu.h>
#include <string.h>
#include <malloc.h>
#include <elf.h>  // now a header-only or shared header
#include <zuzu/zxf.h>
#include <zuzu/memprot.h>
#include <zuzu/user_layout.h>

/* Shared by both the ELF and ZXF loaders below: injects one PT_LOAD-like
 * segment (file-backed portion + demand-zero BSS tail) given format-neutral
 * fields already pulled out of an Elf32_Phdr or a ZXFSegment. */
static int inject_segment(uint32_t taskHandle, const void *data, size_t data_size,
                          uint32_t vaddr, uint32_t file_offset, uint32_t file_size,
                          uint32_t mem_size, uint32_t prot)
{
    if (file_offset + file_size > data_size)
        return -1;

    // inject file-backed portion
    if (file_size > 0) {
        int32_t rc = ZuzuAsInject(taskHandle, vaddr,
                               (const uint8_t *)data + file_offset,
                               file_size, prot);
        if (rc != 0) return rc;
    }

    // BSS: memsz > filesz means zero-filled pages beyond the file data,
    // with no file content behind them. asinject already zeroes the
    // partial tail of the boundary page (the one holding file_size), so the
    // rest just needs to be reserved as demand-zero anon memory - no bytes
    // to copy, so no need to materialize a zero buffer.
    uint32_t file_end = vaddr + file_size;
    uint32_t mem_end  = vaddr + mem_size;
    uint32_t bss_start = (file_end + 0xFFF) & ~0xFFF;  // next page boundary

    if (bss_start < mem_end) {
        size_t bss_len = mem_end - bss_start;
        bss_len = (bss_len + 0xFFF) & ~0xFFF;  // round up to page

        int32_t rc = ZuzuAsInjectReserve(taskHandle, bss_start, bss_len, prot);
        if (rc != 0) return rc;
    }

    return 0;
}

static int inject_stack(uint32_t taskHandle,
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

    int32_t rc = ZuzuAsInject(taskHandle, img_base, buf, USER_STACK_SIZE,
                           PROT_READ | PROT_WRITE);
    free(buf);
    if (rc != 0) return rc;

    *out_sp = sp;
    *out_argv = argv_va;
    return 0;
}

static int exec_inject_elf(uint32_t taskHandle, const void *elf_data, size_t elf_size,
                           uint32_t *out_entry)
{
    uint32_t entry = elf_validate(elf_data, elf_size);
    if (!entry) return -1;

    int phdr_count = elf_phdr_count(elf_data);

    // check for overlapping segments
    for (int i = 0; i < phdr_count; i++) {
        const Elf32_Phdr *a = elf_phdr_get(elf_data, i);
        if (a->p_type != PT_LOAD) continue;
        for (int j = i + 1; j < phdr_count; j++) {
            const Elf32_Phdr *b = elf_phdr_get(elf_data, j);
            if (b->p_type != PT_LOAD) continue;
            uint32_t a_end = a->p_vaddr + a->p_memsz;
            uint32_t b_end = b->p_vaddr + b->p_memsz;
            if (a->p_vaddr < b_end && b->p_vaddr < a_end)
                return -1;
        }
    }

    // inject each PT_LOAD segment
    for (int i = 0; i < phdr_count; i++) {
        const Elf32_Phdr *ph = elf_phdr_get(elf_data, i);
        if (ph->p_type != PT_LOAD) continue;
        uint32_t prot = 0;
        if (ph->p_flags & PF_R) prot |= PROT_READ;
        if (ph->p_flags & PF_W) prot |= PROT_WRITE;
        if (ph->p_flags & PF_X) prot |= PROT_EXEC;
        int rc = inject_segment(taskHandle, elf_data, elf_size,
                                ph->p_vaddr, ph->p_offset, ph->p_filesz, ph->p_memsz, prot);
        if (rc != 0) return rc;
    }

    *out_entry = entry;
    return 0;
}

static int exec_inject_zxf(uint32_t taskHandle, const void *zxf_data, size_t zxf_size,
                           uint32_t *out_entry)
{
    ZXFImage img;
    if (!ZxfParse(zxf_data, zxf_size, &img)) return -1;

    // check for overlapping segments
    for (int i = 0; i < img.seg_count; i++) {
        const ZXFSegment *a = &img.segs[i];
        for (int j = i + 1; j < img.seg_count; j++) {
            const ZXFSegment *b = &img.segs[j];
            uint64_t a_end = a->vaddr + a->mem_size;
            uint64_t b_end = b->vaddr + b->mem_size;
            if (a->vaddr < b_end && b->vaddr < a_end)
                return -1;
        }
    }

    // inject each segment
    for (int i = 0; i < img.seg_count; i++) {
        const ZXFSegment *seg = &img.segs[i];
        uint32_t prot = 0;
        if (seg->flags & ZXF_R) prot |= PROT_READ;
        if (seg->flags & ZXF_W) prot |= PROT_WRITE;
        if (seg->flags & ZXF_X) prot |= PROT_EXEC;
        int rc = inject_segment(taskHandle, zxf_data, zxf_size,
                                (uint32_t)seg->vaddr, seg->file_offset, seg->file_size,
                                seg->mem_size, prot);
        if (rc != 0) return rc;
    }

    *out_entry = img.entry;
    return 0;
}

int exec_inject(uint32_t taskHandle, const void *data, size_t size,
              const char *argbuf, size_t argbuf_len, uint32_t argc, ExecReply *out)
{
    uint32_t entry;
    int rc;

    if (size >= 4 && memcmp(data, "\x7a" "ZXF", 4) == 0)
        rc = exec_inject_zxf(taskHandle, data, size, &entry);
    else
        rc = exec_inject_elf(taskHandle, data, size, &entry);
    if (rc != 0) return rc;

    // inject user stack with argv
    uintptr_t sp = USER_STACK_TOP;
    uintptr_t argv_va = 0;
    rc = inject_stack(taskHandle, argbuf, argbuf_len, argc, &sp, &argv_va);
    if (rc != 0) return rc;

    out->entry = entry;
    out->sp = sp;
    out->argc = argc;
    out->argv_va = argv_va;
    out->pid = 0;
    return 0;
}