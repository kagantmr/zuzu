#ifndef ZUZU_DIRENT_SHIM_H
#define ZUZU_DIRENT_SHIM_H

/* Minimal opendir/readdir/closedir shim for tier-2 (newlib) programs.
 *
 * This newlib build's own <dirent.h> hard #errors on this target (no host
 * directory backend), so this project-local header shadows it (see
 * mk/user.mk's $(NEWLIB_INC)/dirent.h symlink rule) and lib/posix/dirent.c
 * implements it directly over fsd's FSD_READDIR wire op via fsd_client.h --
 * the same call zzsh's own `ls` already uses.
 */

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct DIR DIR;

    struct dirent
    {
        unsigned char d_type; /* FsdFileType: FSD_TYPE_FILE/DIR/SYMLINK */
        char d_name[56];      /* matches FsdDirEntry.name */
    };

    DIR *opendir(const char *path);
    struct dirent *readdir(DIR *dirp);
    int closedir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif
