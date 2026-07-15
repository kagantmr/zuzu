// cpio.h - CPIO archive handling

#ifndef CPIO_H
#define CPIO_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cpio_header
{
    char magic[6];     // "070701" for new ASCII format
    char ino[8];       // inode number
    char mode[8];      // file mode
    char uid[8];       // user ID of owner
    char gid[8];       // group ID of owner
    char nlink[8];     // number of links
    char mtime[8];     // modification time
    char filesize[8];  // size of the file in bytes
    char devmajor[8];  // major device number
    char devminor[8];  // minor device number
    char rdevmajor[8]; // major device number for special files
    char rdevminor[8]; // minor device number for special files
    char namesize[8];  // length of the filename, including null terminator
    char check[8];     // always set to zero
} cpio_hdr_t;          // exactly 110 bytes

/**
 * @brief Finds a file in a CPIO archive.
 * 
 * @param base Pointer to the start of the CPIO archive in memory.
 * @param size Size of the CPIO archive in bytes.
 * @param name Name of the file to find (null-terminated string).
 * @param data_out Pointer to a variable that will receive the pointer to the file's data
 * @param size_out Pointer to a variable that will receive the size of the file's data
 * @return true if the file was found, false otherwise.
 */
bool cpio_find(const void *base, size_t size, const char *name,
               const void **data_out, size_t *size_out);

#ifdef __cplusplus
}
#endif

#endif