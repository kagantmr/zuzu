/**
 * @file
 * @brief Starting programs: load a ZXF or ELF32 image into a new space and run it.
 */

#ifndef ZUZU_SPAWN_H
#define ZUZU_SPAWN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <types.h>

/**
 * @brief Starts a program from an image in memory.
 *
 * Detects ZXF or ELF32 by its magic, creates a space and a task, maps the
 * load segments, lays out argv on a fresh stack, gives the new space the
 * name-service port in its handle slot 0, and starts the task with r0 = argc
 * and r1 = argv.
 *
 * @param[in]  image      Image bytes, 4-byte aligned.
 * @param[in]  size       Image size in bytes.
 * @param[in]  name       Name for the new space; truncated to 31 characters.
 * @param[in]  argbuf     argv strings back to back, each NUL-terminated ("a\0b\0"), or NULL.
 * @param[in]  argbuf_len Length of @p argbuf including the NULs, or 0.
 * @param[in]  argc       Number of strings in @p argbuf, or 0.
 * @param[out] out_pid    Receives the new space's handle; may be NULL.
 * @param[out] out_task   Receives the new task's handle; may be NULL.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_MALFORMED The image is neither a valid ZXF nor a valid ARM ELF32 executable.
 * @retval ERR_OVERFLOW  The image has more than 8 load segments.
 * @retval ERR_BADARG    @p argc and @p argbuf disagree, or argv needs more than 16 KiB.
 * @retval ERR_NOMEM     Out of memory.
 */
Err SpawnProcess(const void *image, size_t size, const char *name, const char *argbuf,
                 size_t argbuf_len, uint32_t argc, Spid *out_pid, Handle *out_task);

/** @brief SpawnAttr value meaning "keep what the new task inherits from the caller". */
#define SPAWN_PRIO_INHERIT (-1)

/** @brief Scheduling settings for a new program. */
typedef struct {
    int32_t prio;     /**< Base priority, or SPAWN_PRIO_INHERIT. */
    int32_t max_prio; /**< Maximum priority, or SPAWN_PRIO_INHERIT. */
} SpawnAttr;

/**
 * @brief SpawnProcess() with scheduling settings.
 *
 * @p prio must not exceed @p max_prio, and neither may exceed the caller's own
 * maximum priority.
 *
 * @param[in]  image      Image bytes, 4-byte aligned.
 * @param[in]  size       Image size in bytes.
 * @param[in]  name       Name for the new space.
 * @param[in]  argbuf     argv strings back to back, or NULL.
 * @param[in]  argbuf_len Length of @p argbuf, or 0.
 * @param[in]  argc       Number of strings in @p argbuf, or 0.
 * @param[in]  attr       Scheduling settings, or NULL to inherit both.
 * @param[out] out_pid    Receives the new space's handle; may be NULL.
 * @param[out] out_task   Receives the new task's handle; may be NULL.
 * @return As SpawnProcess(); also ERR_BADARG if prio is above max_prio, or
 *         ERR_NOPERM if either is above the caller's maximum.
 */
Err SpawnProcessAttr(const void *image, size_t size, const char *name, const char *argbuf,
                     size_t argbuf_len, uint32_t argc, const SpawnAttr *attr, Spid *out_pid,
                     Handle *out_task);

/**
 * @brief SpawnProcess() from an image held in a memory object, e.g. read there by FsdReadObj().
 *
 * Read-only segments whose file offset is page-aligned are mapped straight
 * from the object, without copying; the rest is copied. The caller keeps
 * @p image_obj and may close it afterwards: the new program's mappings hold
 * their own references.
 *
 * @param[in]  image_obj  Memory object holding the image.
 * @param[in]  size       Image size in bytes.
 * @param[in]  name       Name for the new space.
 * @param[in]  argbuf     argv strings back to back, or NULL.
 * @param[in]  argbuf_len Length of @p argbuf, or 0.
 * @param[in]  argc       Number of strings in @p argbuf, or 0.
 * @param[out] out_pid    Receives the new space's handle; may be NULL.
 * @param[out] out_task   Receives the new task's handle; may be NULL.
 * @return As SpawnProcess(), or the error from mapping @p image_obj.
 */
Err SpawnProcessObj(Handle image_obj, size_t size, const char *name, const char *argbuf,
                    size_t argbuf_len, uint32_t argc, Spid *out_pid, Handle *out_task);

/**
 * @brief SpawnProcessObj() with scheduling settings; see SpawnProcessAttr().
 *
 * @param[in]  image_obj  Memory object holding the image.
 * @param[in]  size       Image size in bytes.
 * @param[in]  name       Name for the new space.
 * @param[in]  argbuf     argv strings back to back, or NULL.
 * @param[in]  argbuf_len Length of @p argbuf, or 0.
 * @param[in]  argc       Number of strings in @p argbuf, or 0.
 * @param[in]  attr       Scheduling settings, or NULL to inherit both.
 * @param[out] out_pid    Receives the new space's handle; may be NULL.
 * @param[out] out_task   Receives the new task's handle; may be NULL.
 * @return As SpawnProcessAttr().
 */
Err SpawnProcessObjAttr(Handle image_obj, size_t size, const char *name, const char *argbuf,
                        size_t argbuf_len, uint32_t argc, const SpawnAttr *attr, Spid *out_pid,
                        Handle *out_task);

#ifdef __cplusplus
}
#endif

#endif
