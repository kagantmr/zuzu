/**
 * @file
 * @brief Program loading: image parsing, segment mapping and argv layout.
 */

#include <elf.h>
#include <stdbool.h>
#include <util/spawn.h>
#include <util/tls.h>
#include <util/zxf.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/user_layout.h>
#include <zuzu/zuzu.h>

#include <malloc.h>
#include <string.h>

/** @brief Rounds a size up to a whole number of pages. */
#define PAGE_ROUND_UP(x) (((x) + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1))
/** @brief Rounds an address down to its page. */
#define PAGE_ROUND_DOWN(x) ((x) & ~(VirtAddr)(PAGE_SIZE - 1))

/** @brief Most load segments an image may have. */
#define SPAWN_MAX_SEGS 8

/** @brief A load segment, in a form common to ZXF and ELF. */
typedef struct {
    uint32_t file_offset; /**< Offset of the segment's bytes in the image. */
    uint32_t file_size;   /**< Bytes present in the image. */
    uint32_t vaddr;       /**< Page-aligned load address. */
    uint32_t mem_size;    /**< Size in memory; the rest past file_size is zero-filled. */
    uint32_t prot;        /**< PROT_READ / PROT_WRITE / PROT_EXEC. */
} SpawnSeg;

/** @brief A parsed image: entry point and load segments. */
typedef struct {
    uint32_t entry;                /**< Entry point address. */
    uint32_t seg_count;            /**< Number of entries in @c segs. */
    SpawnSeg segs[SPAWN_MAX_SEGS]; /**< Load segments. */
} SpawnImage;

/**
 * @brief Checks that a segment's file range lies inside the image and its address is page-aligned.
 *
 * @param[in] s    Segment to check.
 * @param[in] size Image size in bytes.
 * @return true if the segment is usable.
 */
static bool SegInBounds(const SpawnSeg *s, size_t size)
{
    return s->file_size <= s->mem_size && s->file_offset <= size &&
           s->file_size <= size - s->file_offset && (s->vaddr & (PAGE_SIZE - 1)) == 0;
}

/**
 * @brief Reads the load segments of a ZXF image.
 *
 * @param[in]  data Image bytes.
 * @param[in]  size Image size in bytes.
 * @param[out] out  Entry point and segments.
 * @return ZUZU_OK, ERR_MALFORMED for an invalid image, or ERR_OVERFLOW for too many segments.
 */
static Err ParseZxf(const void *data, size_t size, SpawnImage *out)
{
    ZXFImage img;
    if (!ZxfParse(data, size, &img))
        return ERR_MALFORMED;
    if (img.seg_count > SPAWN_MAX_SEGS)
        return ERR_OVERFLOW;

    out->entry = img.entry;
    out->seg_count = img.seg_count;
    for (uint32_t i = 0; i < img.seg_count; i++) {
        const ZXFSegment *z = &img.segs[i];
        uint32_t prot = 0;
        if (z->flags & ZXF_R)
            prot |= PROT_READ;
        if (z->flags & ZXF_W)
            prot |= PROT_WRITE;
        if (z->flags & ZXF_X)
            prot |= PROT_EXEC;
        out->segs[i] =
            (SpawnSeg){z->file_offset, z->file_size, (uint32_t)z->vaddr, z->mem_size, prot};
    }
    return ZUZU_OK;
}

/**
 * @brief Reads the PT_LOAD segments of an ARM ELF32 executable.
 *
 * @param[in]  data Image bytes.
 * @param[in]  size Image size in bytes.
 * @param[out] out  Entry point and segments.
 * @return ZUZU_OK, ERR_MALFORMED for an invalid image or segment, or ERR_OVERFLOW for too many segments.
 */
static Err ParseElf(const void *data, size_t size, SpawnImage *out)
{
    uint32_t entry = ElfValidate(data, size);
    if (entry == 0)
        return ERR_MALFORMED;

    out->entry = entry;
    out->seg_count = 0;
    int n = ElfPhdrCount(data);
    for (int i = 0; i < n; i++) {
        const Elf32_Phdr *ph = ElfPhdrGet(data, i);
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
        SpawnSeg seg = {ph->p_offset, ph->p_filesz, ph->p_vaddr, ph->p_memsz, prot};
        if (!SegInBounds(&seg, size))
            return ERR_MALFORMED;
        out->segs[out->seg_count++] = seg;
    }
    return out->seg_count ? ZUZU_OK : ERR_MALFORMED;
}

/**
 * @brief Parses an image as ELF if it starts with the ELF magic, otherwise as ZXF.
 *
 * @param[in]  data Image bytes.
 * @param[in]  size Image size in bytes.
 * @param[out] out  Entry point and segments.
 * @return As ParseElf() or ParseZxf().
 */
static Err ParseImage(const void *data, size_t size, SpawnImage *out)
{
    if (size >= 4 && memcmp(data, ELF_MAGIC, 4) == 0)
        return ParseElf(data, size, out);
    return ParseZxf(data, size, out);
}

/** @brief Most pages the argv block (pointer table plus strings) may use. */
#define SPAWN_ARGV_MAX_PAGES 4u
/** @brief Where the argv block is placed in the new space, just below the program. */
#define SPAWN_ARGV_VA (USER_ELF_BASE - (SPAWN_ARGV_MAX_PAGES * PAGE_SIZE))

/**
 * @brief Maps one segment into the new space.
 *
 * Read-only, page-aligned segments are mapped straight from @p image_obj
 * when there is one; otherwise the bytes are copied. Any part of the
 * segment beyond its file bytes becomes demand-zero memory.
 *
 * @param[in] space_handle New space.
 * @param[in] image_obj    Memory object holding the image, or -1.
 * @param[in] data         Image bytes.
 * @param[in] size         Image size in bytes.
 * @param[in] seg          Segment to map.
 * @return ZUZU_OK, or the error from MemInjectObj() / MemInjectBytes().
 */
static Err LoadSegment(Handle space_handle, Handle image_obj, const void *data, size_t size,
                       const SpawnSeg *seg)
{
    uint32_t prot = seg->prot;

    size_t file_pages = PAGE_ROUND_UP(seg->file_size) / PAGE_SIZE;
    size_t mem_pages = PAGE_ROUND_UP(seg->mem_size) / PAGE_SIZE;

    bool direct = image_obj >= 0 && file_pages > 0 && !(prot & PROT_WRITE) &&
                  (seg->file_offset & (PAGE_SIZE - 1)) == 0 &&
                  seg->file_offset + (file_pages * PAGE_SIZE) <= PAGE_ROUND_UP(size);

    if (direct) {
        Err rc = MemInjectObj(space_handle, image_obj, (VirtAddr)seg->vaddr, seg->file_offset,
                              file_pages * PAGE_SIZE, prot);
        if (rc != ZUZU_OK)
            return rc;
    } else if (file_pages > 0) {
        Err rc = MemInjectBytes(space_handle, (VirtAddr)seg->vaddr,
                                (const uint8_t *)data + seg->file_offset, seg->file_size, prot);
        if (rc != ZUZU_OK)
            return rc;
    }

    if (mem_pages > file_pages) {
        Err rc =
            MemInjectObj(space_handle, HANDLE_ANON, (VirtAddr)seg->vaddr + (file_pages * PAGE_SIZE),
                         0, (mem_pages - file_pages) * PAGE_SIZE, prot);
        if (rc != ZUZU_OK)
            return rc;
    }

    return ZUZU_OK;
}

/**
 * @brief Builds argv in the new space: a NULL-terminated pointer table followed by the strings.
 *
 * @param[in]  space_handle New space.
 * @param[in]  argbuf       argv strings back to back, or NULL.
 * @param[in]  argbuf_len   Length of @p argbuf.
 * @param[in]  argc         Number of strings.
 * @param[out] out_sp       Initial stack pointer for the task.
 * @param[out] out_argv_va  Address of the pointer table in the new space, or 0 if argc is 0.
 * @return ZUZU_OK, ERR_BADARG if argv needs more than SPAWN_ARGV_MAX_PAGES, or ERR_NOMEM.
 */
static Err LayoutArgv(Handle space_handle, const char *argbuf, size_t argbuf_len, uint32_t argc,
                      VirtAddr *out_sp, VirtAddr *out_argv_va)
{
    *out_sp = USR_SP;
    *out_argv_va = 0;

    if (argc == 0)
        return ZUZU_OK;

    size_t table_bytes = (size_t)(argc + 1) * sizeof(uint32_t);
    size_t block_len = table_bytes + argbuf_len;
    if (PAGE_ROUND_UP(block_len) > SPAWN_ARGV_MAX_PAGES * PAGE_SIZE)
        return ERR_BADARG;

    uint8_t *block = malloc(block_len);
    if (!block)
        return ERR_NOMEM;

    VirtAddr str_va = SPAWN_ARGV_VA + table_bytes;
    const char *str_src = argbuf;
    for (uint32_t a = 0; a <= argc; a++) {
        uint32_t slot = (a < argc) ? (uint32_t)str_va : 0;
        memcpy(block + (a * sizeof(uint32_t)), &slot, sizeof(slot));
        if (a < argc) {
            size_t l = strlen(str_src) + 1;
            str_va += l;
            str_src += l;
        }
    }
    memcpy(block + table_bytes, argbuf, argbuf_len);

    Err rc = MemInjectBytes(space_handle, SPAWN_ARGV_VA, block, block_len, PROT_RW);
    free(block);
    if (rc != ZUZU_OK)
        return rc;

    *out_argv_va = SPAWN_ARGV_VA;
    return ZUZU_OK;
}

/**
 * @brief Common body of all SpawnProcess* variants.
 *
 * @param[in]  image_obj  Memory object holding the image, or -1.
 * @param[in]  image      Image bytes.
 * @param[in]  size       Image size in bytes.
 * @param[in]  name       Name for the new space.
 * @param[in]  argbuf     argv strings back to back, or NULL.
 * @param[in]  argbuf_len Length of @p argbuf.
 * @param[in]  argc       Number of strings.
 * @param[in]  attr       Scheduling settings, or NULL.
 * @param[out] out_pid    Receives the new space's handle; may be NULL.
 * @param[out] out_task   Receives the new task's handle; may be NULL.
 * @return As SpawnProcessAttr().
 */
static Err SpawnFromImage(Handle image_obj, const void *image, size_t size, const char *name,
                          const char *argbuf, size_t argbuf_len, uint32_t argc,
                          const SpawnAttr *attr, Spid *out_pid, Handle *out_task)
{
    SpawnImage img;
    Err prc = ParseImage(image, size, &img);
    if (prc != ZUZU_OK)
        return prc;
    if ((argc > 0) != (argbuf != NULL && argbuf_len > 0))
        return ERR_BADARG;
    if (attr && attr->prio != SPAWN_PRIO_INHERIT && attr->max_prio != SPAWN_PRIO_INHERIT &&
        attr->prio > attr->max_prio)
        return ERR_BADARG;

    Handle space_handle = CreateSpace(name);
    if (space_handle < 0)
        return (Err)space_handle;

    for (uint32_t i = 0; i < img.seg_count; i++) {
        Err rc = LoadSegment(space_handle, image_obj, image, size, &img.segs[i]);
        if (rc != ZUZU_OK) {
            HandleDestroy(space_handle);
            return rc;
        }
    }

    VirtAddr sp, argv_va;
    Err rc = LayoutArgv(space_handle, argbuf, argbuf_len, argc, &sp, &argv_va);
    if (rc != ZUZU_OK) {
        HandleDestroy(space_handle);
        return rc;
    }

    Handle task_handle = CreateTask(space_handle);
    if (task_handle < 0) {
        HandleDestroy(space_handle);
        return (Err)task_handle;
    }

    if (attr && attr->max_prio != SPAWN_PRIO_INHERIT) {
        rc = TaskSetMaxPriority(task_handle, (uint32_t)attr->max_prio);
        if (rc != ZUZU_OK) {
            HandleDestroy(space_handle);
            return rc;
        }
    }

    if (attr && attr->prio != SPAWN_PRIO_INHERIT) {
        rc = TaskSetPriority(task_handle, (uint32_t)attr->prio);
        if (rc != ZUZU_OK) {
            HandleDestroy(space_handle);
            return rc;
        }
    }

    rc = HandleGrantTo(NSVC_PORT, space_handle, PERM_SEND | PERM_TXFR, NULL);
    if (rc != ZUZU_OK) {
        HandleDestroy(space_handle);
        return rc;
    }

    rc = TaskStart(task_handle, (void *)img.entry, (void *)sp, argc, (uint32_t)argv_va);
    if (rc != ZUZU_OK) {
        HandleDestroy(space_handle);
        return rc;
    }

    if (out_pid)
        *out_pid = space_handle;
    if (out_task)
        *out_task = task_handle;
    return ZUZU_OK;
}

Err SpawnProcessAttr(const void *image, size_t size, const char *name, const char *argbuf,
                     size_t argbuf_len, uint32_t argc, const SpawnAttr *attr, Spid *out_pid,
                     Handle *out_task)
{
    return SpawnFromImage(-1, image, size, name, argbuf, argbuf_len, argc, attr, out_pid, out_task);
}

Err SpawnProcess(const void *image, size_t size, const char *name, const char *argbuf,
                 size_t argbuf_len, uint32_t argc, Spid *out_pid, Handle *out_task)
{
    return SpawnFromImage(-1, image, size, name, argbuf, argbuf_len, argc, NULL, out_pid, out_task);
}

Err SpawnProcessObjAttr(Handle image_obj, size_t size, const char *name, const char *argbuf,
                        size_t argbuf_len, uint32_t argc, const SpawnAttr *attr, Spid *out_pid,
                        Handle *out_task)
{
    void *view = MemMap(image_obj, 0, PROT_READ);
    if (PtrIsErr(view))
        return (Err)(intptr_t)view;
    Err rc = SpawnFromImage(image_obj, view, size, name, argbuf, argbuf_len, argc, attr, out_pid,
                            out_task);
    MemUnmap(view);
    return rc;
}

Err SpawnProcessObj(Handle image_obj, size_t size, const char *name, const char *argbuf,
                    size_t argbuf_len, uint32_t argc, Spid *out_pid, Handle *out_task)
{
    return SpawnProcessObjAttr(image_obj, size, name, argbuf, argbuf_len, argc, NULL, out_pid,
                               out_task);
}
