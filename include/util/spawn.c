#include <util/spawn.h>
#include <elf.h>
#include <zuzu/service.h>
#include <util/tls.h>
#include <util/zxf.h>
#include <stdbool.h>
#include <zuzu/err.h>
#include <zuzu/user_layout.h>
#include <zuzu/zuzu.h>

#include <malloc.h>
#include <string.h>

#define PAGE_ROUND_UP(x) (((x) + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1))
#define PAGE_ROUND_DOWN(x) ((x) & ~(VirtAddr)(PAGE_SIZE - 1))

#define SPAWN_MAX_SEGS 8

typedef struct
{
    uint32_t file_offset;
    uint32_t file_size;
    uint32_t vaddr;
    uint32_t mem_size;
    uint32_t prot;
} SpawnSeg;

typedef struct
{
    uint32_t entry;
    uint32_t seg_count;
    SpawnSeg segs[SPAWN_MAX_SEGS];
} SpawnImage;

static bool SegInBounds(const SpawnSeg *s, size_t size)
{
    return s->file_size <= s->mem_size && s->file_offset <= size && s->file_size <= size - s->file_offset &&
           (s->vaddr & (PAGE_SIZE - 1)) == 0;
}

static Err ParseZxf(const void *data, size_t size, SpawnImage *out)
{
    ZXFImage img;
    if (!ZxfParse(data, size, &img))
        return ERR_MALFORMED;
    if (img.seg_count > SPAWN_MAX_SEGS)
        return ERR_OVERFLOW;

    out->entry = img.entry;
    out->seg_count = img.seg_count;
    for (uint32_t i = 0; i < img.seg_count; i++)
    {
        const ZXFSegment *z = &img.segs[i];
        uint32_t prot = 0;
        if (z->flags & ZXF_R)
            prot |= PROT_READ;
        if (z->flags & ZXF_W)
            prot |= PROT_WRITE;
        if (z->flags & ZXF_X)
            prot |= PROT_EXEC;
        out->segs[i] = (SpawnSeg){ z->file_offset, z->file_size, (uint32_t)z->vaddr, z->mem_size, prot };
    }
    return ZUZU_OK;
}

static Err ParseElf(const void *data, size_t size, SpawnImage *out)
{
    uint32_t entry = elf_validate(data, size);
    if (entry == 0)
        return ERR_MALFORMED;

    out->entry = entry;
    out->seg_count = 0;
    int n = elf_phdr_count(data);
    for (int i = 0; i < n; i++)
    {
        const Elf32_Phdr *ph = elf_phdr_get(data, i);
        if (ph->p_type != PT_LOAD)
            continue;
        if (out->seg_count == SPAWN_MAX_SEGS)
            return ERR_OVERFLOW;

        uint32_t prot = 0;
        if (ph->p_flags & PF_R)
            prot |= PROT_READ;
        if (ph->p_flags & PF_W)
            prot |= PROT_WRITE;
        if (ph->p_flags & PF_X)
            prot |= PROT_EXEC;
        SpawnSeg seg = { ph->p_offset, ph->p_filesz, ph->p_vaddr, ph->p_memsz, prot };
        if (!SegInBounds(&seg, size))
            return ERR_MALFORMED;
        out->segs[out->seg_count++] = seg;
    }
    return out->seg_count ? ZUZU_OK : ERR_MALFORMED;
}

static Err ParseImage(const void *data, size_t size, SpawnImage *out)
{
    if (size >= 4 && memcmp(data, ELF_MAGIC, 4) == 0)
        return ParseElf(data, size, out);
    return ParseZxf(data, size, out);
}

static Err LoadSegment(Handle space_handle, const void *data, const SpawnSeg *seg)
{
    uint32_t prot = seg->prot;

    size_t file_pages = PAGE_ROUND_UP(seg->file_size) / PAGE_SIZE;
    size_t mem_pages = PAGE_ROUND_UP(seg->mem_size) / PAGE_SIZE;

    if (file_pages > 0)
    {
        /* Zero-padded so the tail of the boundary page (file content mixed
         * with BSS, when file_size isn't page-aligned) comes out zeroed,
         * same as the kernel's own ELF loader. */
        uint8_t *buf = malloc(file_pages * PAGE_SIZE);
        if (!buf)
            return ERR_NOMEM;
        memset(buf, 0, file_pages * PAGE_SIZE);
        memcpy(buf, (const uint8_t *)data + seg->file_offset, seg->file_size);

        Err rc =
            MemInject(space_handle, (VirtAddr)seg->vaddr, buf, file_pages * PAGE_SIZE, prot, 0);
        free(buf);
        if (rc != ZUZU_OK)
            return rc;
    }

    if (mem_pages > file_pages)
    {
        Err rc = MemInject(space_handle, (VirtAddr)seg->vaddr + (file_pages * PAGE_SIZE), NULL,
                           (mem_pages - file_pages) * PAGE_SIZE, prot, ASINJECT_FLAG_RESERVE);
        if (rc != ZUZU_OK)
            return rc;
    }

    return ZUZU_OK;
}

/* Lays out argv the same way kernel/task/kernel_load.c's KernelProcessLoad
 * does: strings just below USR_SP, then the (argc+1)-slot pointer array
 * (NULL-terminated) just below that. Returns the resulting sp and, if
 * argc > 0, the argv pointer array's VA in *out_argv_va. */
static Err LayoutArgv(Handle space_handle, const char *argbuf, size_t argbuf_len, uint32_t argc,
                      VirtAddr *out_sp, VirtAddr *out_argv_va)
{
    VirtAddr sp = USR_SP;
    *out_argv_va = 0;

    if (argc == 0)
    {
        *out_sp = sp;
        return ZUZU_OK;
    }

    sp -= argbuf_len;
    sp &= ~(VirtAddr)3U;
    VirtAddr strings_va = sp;

    sp -= (VirtAddr)(argc + 1) * sizeof(uint32_t);
    sp &= ~(VirtAddr)7U;
    VirtAddr argv_va = sp;

    if (argv_va < USER_STACK_BASE)
        return ERR_BADARG;

    /* Build the whole [ptr array][strings] block locally, at the exact byte
     * offsets it will land at in the target stack, then inject it as one
     * page-aligned write -- MemInject fills in place since this falls
     * entirely inside the stack region SpaceCreate already reserved. */
    VirtAddr block_start = PAGE_ROUND_DOWN(argv_va);
    VirtAddr block_end = PAGE_ROUND_UP(USR_SP);
    size_t block_len = block_end - block_start;

    uint8_t *block = malloc(block_len);
    if (!block)
        return ERR_NOMEM;
    memset(block, 0, block_len);

    VirtAddr str_va = strings_va;
    const char *str_src = argbuf;
    for (uint32_t a = 0; a <= argc; a++)
    {
        uint32_t slot = (a < argc) ? (uint32_t)str_va : 0;
        memcpy(block + (argv_va + a * sizeof(uint32_t) - block_start), &slot, sizeof(slot));
        if (a < argc)
        {
            size_t l = strlen(str_src) + 1;
            str_va += l;
            str_src += l;
        }
    }
    memcpy(block + (strings_va - block_start), argbuf, argbuf_len);

    Err rc = MemInject(space_handle, block_start, block, block_len, PROT_RW, 0);
    free(block);
    if (rc != ZUZU_OK)
        return rc;

    *out_sp = sp;
    *out_argv_va = argv_va;
    return ZUZU_OK;
}

Err SpawnProcess(const void *image, size_t size, const char *name, const char *argbuf,
                 size_t argbuf_len, uint32_t argc, Spid *out_pid, Handle *out_task)
{
    SpawnImage img;
    Err prc = ParseImage(image, size, &img);
    if (prc != ZUZU_OK)
        return prc;
    if ((argc > 0) != (argbuf != NULL && argbuf_len > 0))
        return ERR_BADARG;

    Handle space_handle = CreateSpace(name);
    if (space_handle < 0)
        return (Err)space_handle;

    for (uint32_t i = 0; i < img.seg_count; i++)
    {
        Err rc = LoadSegment(space_handle, image, &img.segs[i]);
        if (rc != ZUZU_OK)
        {
            HandleDestroy(space_handle);
            return rc;
        }
    }

    VirtAddr sp, argv_va;
    Err rc = LayoutArgv(space_handle, argbuf, argbuf_len, argc, &sp, &argv_va);
    if (rc != ZUZU_OK)
    {
        HandleDestroy(space_handle);
        return rc;
    }

    Handle task_handle = CreateTask(space_handle);
    if (task_handle < 0)
    {
        HandleDestroy(space_handle);
        return (Err)task_handle;
    }

    SvcResult grant = HandleGrant(NSVC_PORT, space_handle, PERM_SEND);
    if (grant.r0 != ZUZU_OK)
    {
        HandleDestroy(space_handle);
        return (Err)grant.r0;
    }

    rc = TaskStart(task_handle, (VirtAddr)img.entry, sp, argc, (uint32_t)argv_va);
    if (rc != ZUZU_OK)
    {
        HandleDestroy(space_handle);
        return rc;
    }

    if (out_pid)
        *out_pid =
            space_handle; /* the handle IS the pid-bearing token for the space we just made */
    if (out_task)
        *out_task = task_handle;
    return ZUZU_OK;
}
