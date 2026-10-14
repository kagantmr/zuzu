#ifndef ZUZU_SPAWN_H
#define ZUZU_SPAWN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <types.h>

/**
 * @brief Spawns a program from an in-memory ZXF or ELF32 image, detected by
 * its magic: creates a kitten Space + Task, injects its load segments,
 * reserves and lays out its initial stack (with an optional argv), then
 * kickstarts it.
 *
 * @param image     Pointer to the ZXF or ELF image bytes (4-byte aligned).
 * @param size      Size of image in bytes.
 * @param name      Name for the new Space (truncated to its name field).
 * @param argbuf    NUL-delimited argv strings ("arg0\0arg1\0..."), or NULL.
 * @param argbuf_len Length of argbuf including NULs, or 0.
 * @param argc      Number of strings in argbuf, or 0.
 * @param out_pid   Set to the new Space's pid on success.
 * @param out_task  Set to the new Task's handle on success, or NULL.
 * @return ZUZU_OK on success, ERR_MALFORMED if the image is neither a valid
 * ZXF nor a valid ARM ELF32 executable, or another negative Err.
 */
Err SpawnProcess(const void *image, size_t size, const char *name, const char *argbuf,
                 size_t argbuf_len, uint32_t argc, Spid *out_pid, Handle *out_task);

#define SPAWN_PRIO_INHERIT (-1)

typedef struct {
    int32_t prio;
    int32_t max_prio;
} SpawnAttr;

/**
 * @brief SpawnProcess() with explicit scheduling attributes. A field set to SPAWN_PRIO_INHERIT
 * keeps the value the new task inherits from the caller. prio must not exceed max_prio, and
 * neither may exceed the caller's own max priority.
 */
Err SpawnProcessAttr(const void *image, size_t size, const char *name, const char *argbuf,
                     size_t argbuf_len, uint32_t argc, const SpawnAttr *attr, Spid *out_pid,
                     Handle *out_task);

/**
 * @brief Like SpawnProcess(), but the image is already in a shm object (for example read there
 * by FsdReadObj). Read-only and executable segments whose file offset is page-aligned are mapped
 * straight from the object with no copy; everything else is copied as in SpawnProcess().
 *
 * @param image_obj A shm object handle holding the image; the caller keeps it and may close it
 * once this returns, since the child's mappings hold their own references.
 * @param size      Size of the image in bytes.
 * @return As SpawnProcess().
 */
Err SpawnProcessObj(Handle image_obj, size_t size, const char *name, const char *argbuf,
                    size_t argbuf_len, uint32_t argc, Spid *out_pid, Handle *out_task);

/**
 * @brief SpawnProcessObj() with explicit scheduling attributes; see SpawnProcessAttr().
 */
Err SpawnProcessObjAttr(Handle image_obj, size_t size, const char *name, const char *argbuf,
                        size_t argbuf_len, uint32_t argc, const SpawnAttr *attr, Spid *out_pid,
                        Handle *out_task);

#ifdef __cplusplus
}
#endif

#endif
