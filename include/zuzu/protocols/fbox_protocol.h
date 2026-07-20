#ifndef FBOX_PROTOCOL_H
#define FBOX_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zuzu/protocols/fat32d_protocol.h>

/* Commands, same numbering as fat32d for simplicity */
#define FBOX_OPEN    1
#define FBOX_READ    2
#define FBOX_WRITE   3
#define FBOX_CLOSE   4
#define FBOX_READDIR 5
#define FBOX_STAT    6
#define FBOX_GET_BUF 7
#define FBOX_SEEK    8

/* Reuse fat32d packing macros and types */
#define FBOX_PACK_RW  FAT32_PACK_RW
#define FBOX_RW_FD    FAT32_RW_FD
#define FBOX_RW_COUNT FAT32_RW_COUNT

typedef fat32_dirent_t fbox_dirent_t;
typedef fat32_stat_t   fbox_stat_t;

/* Status codes mirror fat32d: ZUZU_OK on success, err_t values from
 * <zuzu/err.h> or FAT32_ERR_IO on failure. */

#ifdef __cplusplus
}
#endif

#endif