#ifndef ZUZU_NEWLIB_GETLINE_H
#define ZUZU_NEWLIB_GETLINE_H

/* Force-included into every tier-2 (newlib) translation unit (see
 * -include in mk/user.mk): this newlib build implements getline() as
 * __getline() internally (see lib/posix/stubs.c) but never declares the
 * POSIX name in <stdio.h> under any feature-test macro. */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

ssize_t getline(char **lineptr, size_t *n, FILE *stream);

/* Likewise: lstat() has no <sys/stat.h> declaration in this newlib build
 * at all (see the alias in lib/posix/stubs.c). */
int lstat(const char *path, struct stat *buf);

#endif
