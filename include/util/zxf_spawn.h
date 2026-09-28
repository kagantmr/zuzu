#ifndef ZUZU_ZXF_SPAWN_H
#define ZUZU_ZXF_SPAWN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <types.h>

/**
 * @brief Parses a ZXF blob and spawns it directly: creates a kitten Space +
 * Task, injects its load segments, reserves and lays out its initial stack
 * (with an optional argv), then kickstarts it. Replaces the old sysd
 * SYSD_EXEC RPC -- under Prowl's capability model spawning doesn't need a
 * privileged broker in the middle, just a handle to inject into.
 *
 * @param zxf_data  Pointer to the ZXF image bytes.
 * @param zxf_size  Size of zxf_data in bytes.
 * @param name      Name for the new Space (truncated to its name field).
 * @param argbuf    NUL-delimited argv strings ("arg0\0arg1\0..."), or NULL.
 * @param argbuf_len Length of argbuf including NULs, or 0.
 * @param argc      Number of strings in argbuf, or 0.
 * @param out_pid   Set to the new Space's pid on success.
 * @return ZUZU_OK on success, negative Err on failure.
 */
Err ZxfSpawn(const void *zxf_data, size_t zxf_size, const char *name,
             const char *argbuf, size_t argbuf_len, uint32_t argc, Spid *out_pid);

#ifdef __cplusplus
}
#endif

#endif
