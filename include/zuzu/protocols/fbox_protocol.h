#ifndef FBOX_PROTOCOL_H
#define FBOX_PROTOCOL_H

#include <stdint.h>
#include <zuzu/protocols/fat32d_protocol.h>

/* Commands — same numbering as fat32d for simplicity */
#define FBOX_OPEN    1
#define FBOX_READ    2
#define FBOX_WRITE   3
#define FBOX_CLOSE   4
#define FBOX_READDIR 5
#define FBOX_STAT    6
#define FBOX_GET_BUF 7

/* Reuse fat32d packing macros and types */
#define FBOX_PACK_RW  FAT32_PACK_RW
#define FBOX_RW_FD    FAT32_RW_FD
#define FBOX_RW_COUNT FAT32_RW_COUNT

typedef fat32_dirent_t fbox_dirent_t;
typedef fat32_stat_t   fbox_stat_t;

/* Error codes */
#define FBOX_OK          0
#define FBOX_ERR_NOENT  (-1)
#define FBOX_ERR_ISDIR  (-2)
#define FBOX_ERR_NOTDIR (-3)
#define FBOX_ERR_BADFD  (-4)
#define FBOX_ERR_MAXFD  (-5)
#define FBOX_ERR_IO     (-6)

#endif