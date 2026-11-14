/**
 * @file path.h
 *
 * @brief Absolute path handling shared by the shell and the C library: joining a path onto a
 * working directory and cleaning up "." and ".." parts.
 */

#ifndef UTIL_PATH_H
#define UTIL_PATH_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Longest absolute path the helpers produce, including the terminator. */
#define PATH_MAX_LEN 256

/**
 * @brief Cleans an absolute path: collapses repeated slashes and resolves "." and "..".
 *
 * @param[in]  path     The path; an empty one means "/".
 * @param[out] out      Receives the cleaned path, always starting with '/'.
 * @param[in]  out_size Size of @p out.
 * @return true on success, false if a part or the whole path does not fit.
 */
bool PathNormalize(const char *path, char *out, size_t out_size);

/**
 * @brief Turns @p path into a clean absolute path, taking a relative one from @p cwd.
 *
 * @param[in]  cwd      The working directory, absolute; NULL or empty means "/".
 * @param[in]  path     The path; an empty one means @p cwd itself.
 * @param[out] out      Receives the absolute path.
 * @param[in]  out_size Size of @p out.
 * @return true on success, false if it does not fit.
 */
bool PathResolve(const char *cwd, const char *path, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_PATH_H */
