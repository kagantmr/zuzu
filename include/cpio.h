#ifndef CPIO_H
#define CPIO_H

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

bool cpio_find(const void *base, size_t size, const char *name,
               const void **data_out, size_t *size_out);


#endif