/**
 * @file
 * @brief POSIX declarations this newlib build leaves out.
 *
 * Force-included into every tier-2 (newlib) translation unit; see -include in
 * scripts/Makefile.user. The definitions live in lib/posix/stubs.c.
 */

#ifndef ZUZU_NEWLIB_GETLINE_H
#define ZUZU_NEWLIB_GETLINE_H

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * @brief POSIX getline(); newlib implements it as __getline() but never declares it.
 *
 * @param[in,out] lineptr Buffer, grown with realloc() as needed; *lineptr may be NULL.
 * @param[in,out] n       Size of *lineptr.
 * @param[in]     stream  Stream to read.
 * @return Characters read, including the newline, or -1 at end of file or on error.
 */
ssize_t getline(char **lineptr, size_t *n, FILE *stream);

/**
 * @brief POSIX lstat(); this build makes it an alias of stat().
 *
 * @param[in]  path Path.
 * @param[out] buf  Result.
 * @return 0, or -1 with errno set.
 */
int lstat(const char *path, struct stat *buf);

#endif
