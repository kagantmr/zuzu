/**
 * @file
 * @brief Finding files in a cpio archive (new ASCII format), as used for the initrd.
 */

#ifndef CPIO_H
#define CPIO_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A cpio member header: 110 bytes, numbers as 8 hex digits. */
typedef struct cpio_header {
    char magic[6];     /**< "070701". */
    char ino[8];       /**< Inode number. */
    char mode[8];      /**< File mode. */
    char uid[8];       /**< Owner. */
    char gid[8];       /**< Group. */
    char nlink[8];     /**< Link count. */
    char mtime[8];     /**< Modification time. */
    char filesize[8];  /**< Size of the file data. */
    char devmajor[8];  /**< Major number of the device holding the file. */
    char devminor[8];  /**< Minor number of that device. */
    char rdevmajor[8]; /**< Major number, for device files. */
    char rdevminor[8]; /**< Minor number, for device files. */
    char namesize[8];  /**< Length of the name that follows, including the NUL. */
    char check[8];     /**< Always 0. */
} cpio_hdr_t;

/**
 * @brief Finds a file in a CPIO archive.
 *
 * @param[in]  base     Start of the archive in memory.
 * @param[in]  size     Size of the archive in bytes.
 * @param[in]  name     Name of the file to find.
 * @param[out] data_out Receives a pointer to the file's data, inside the archive.
 * @param[out] size_out Receives the size of the file's data.
 * @return true if the file was found, false otherwise.
 */
bool CpioFind(const void *base, size_t size, const char *name, const void **data_out,
               size_t *size_out);

#ifdef __cplusplus
}
#endif

#endif