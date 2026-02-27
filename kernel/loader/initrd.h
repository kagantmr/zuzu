#ifndef INITRD_H
#define INITRD_H

#include <stddef.h>
#include <stdbool.h>


typedef struct cpio_header {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
} cpio_hdr_t; // exactly 110 bytes


/**
 * @brief Initialize the initrd subsystem with the given archive.
 */
void initrd_init(const void *start, size_t size);

/**
 * @brief Find a file in the initrd archive.
 * 
 * @param name      The name of the file to find (e.g., "kernel.bin").
 * @param data_out  Output parameter that will point to the file's data if found.
 * @param size_out  Output parameter that will contain the size of the file if found.
 * @return true if the file was found and output parameters are set, false otherwise.
 */
bool initrd_find(const char *name, const void **data_out, size_t *size_out);

#endif // INITRD_H