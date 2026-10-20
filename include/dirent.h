#ifndef ZUZU_DIRENT_SHIM_H
#define ZUZU_DIRENT_SHIM_H

/**
 * @file
 * @brief Minimal opendir/readdir/closedir for tier-2 (newlib) programs.
 *
 * newlib's own <dirent.h> refuses to build on this target, so this header
 * shadows it. lib/posix/dirent.c implements it with FsdReadDir().
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief An open directory; opaque. */
typedef struct DIR DIR;

/** @brief One directory entry. */
struct dirent {
    unsigned char d_type; /**< An FsdFileType. */
    char d_name[56];      /**< NUL-terminated name, as in FsdDirEntry. */
};

/**
 * @brief Opens a directory for reading.
 *
 * @param[in] path Directory path.
 * @return The directory, or NULL with errno set.
 */
DIR *opendir(const char *path);
/**
 * @brief Returns the next entry.
 *
 * @param[in,out] dirp Directory from opendir().
 * @return The entry, valid until the next call, or NULL at the end.
 */
struct dirent *readdir(DIR *dirp);
/**
 * @brief Closes a directory.
 *
 * @param[in] dirp Directory from opendir().
 * @return 0.
 */
int closedir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif
