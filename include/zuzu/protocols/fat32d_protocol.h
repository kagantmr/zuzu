#ifndef FAT32D_PROTOCOL_H
#define FAT32D_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <zuzu/err.h>

/* Commands (passed in r2 / cmd field) */
#define FAT32_OPEN    1   /* shmem=path, arg=mode      → r1=status, r2=fd          */
#define FAT32_READ    2   /* arg=fd|(count<<16)         → r1=status, r2=bytes_read; shmem=data */
#define FAT32_WRITE   3   /* arg=fd|(count<<16); shmem=data → r1=status, r2=bytes_written */
#define FAT32_CLOSE   4   /* arg=fd                     → r1=status                 */
#define FAT32_READDIR 5   /* shmem=path                 → r1=status, r2=count; shmem=dirents */
#define FAT32_STAT    6   /* shmem=path                 → r1=status; shmem=fat32_stat_t */
#define FAT32_GET_BUF 7   /* → r1=status, r2=shmem_handle */

/* Open modes (match FatFs FA_* for simplicity) */
#define FAT32_MODE_READ   0x01
#define FAT32_MODE_WRITE  0x02
#define FAT32_MODE_CREATE  0x04

/* Arg packing for READ/WRITE */
#define FAT32_PACK_RW(fd, count)  ((uint32_t)(fd) | ((uint32_t)(count) << 16))
#define FAT32_RW_FD(arg)          ((arg) & 0xFFFF)
#define FAT32_RW_COUNT(arg)       ((arg) >> 16)

/* Status codes: success is ZUZU_OK; failures use err_t values from
 * <zuzu/err.h> (ERR_NOENT, ERR_MALFORMED for a bad fd, ERR_BUFFULL when the
 * fd table is full, ...). The one filesystem-specific code below has no err_t
 * equivalent and lives outside the err_t range to avoid collisions. */
#define FAT32_ERR_IO     (-100)  /* backend FatFs / disk I/O failure */

/* Directory entry returned in shmem by READDIR */
typedef struct {
    char     name[60];
    size_t size;
    uint8_t  is_dir;
    uint8_t  _pad[3];
} fat32_dirent_t;  /* 68 bytes, ~60 per 4096 page */

/* Stat result returned in shmem by STAT */
typedef struct {
    uint32_t size;
    uint8_t  is_dir;
    uint8_t  _pad[3];
} fat32_stat_t;

#endif