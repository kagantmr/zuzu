#ifndef FAT32D_PROTOCOL_H
#define FAT32D_PROTOCOL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stddef.h>
#include <zuzu/err.h>

/* Commands (passed in r2 / cmd field) */
#define FAT32_OPEN 1    /* shmem=path, arg=mode           -> r1=status, r2=fd          */
#define FAT32_READ 2    /* arg=fd|(count<<16)             -> r1=status, r2=bytes_read; shmem=data */
#define FAT32_WRITE 3   /* arg=fd|(count<<16); shmem=data -> r1=status, r2=bytes_written */
#define FAT32_CLOSE 4   /* arg=fd                         -> r1=status                 */
#define FAT32_READDIR 5 /* shmem=path                     -> r1=status, r2=count; shmem=dirents */
#define FAT32_STAT 6    /* shmem=path                     -> r1=status; shmem=fat32_stat_t */
#define FAT32_GET_BUF 7 /*                                -> r1=status, r2=shmem_handle */

/* Open modes (match FatFs FA_* for simplicity) */
#define FAT32_MODE_READ 0x01
#define FAT32_MODE_WRITE 0x02
#define FAT32_MODE_CREATE 0x04

/* Arg packing for READ/WRITE */
#define FAT32_PACK_RW(fd, count) ((uint32_t)(fd) | ((uint32_t)(count) << 16))
#define FAT32_RW_FD(arg) ((arg) & 0xFFFF)
#define FAT32_RW_COUNT(arg) ((arg) >> 16)

#define FAT32_ERR_IO ERR_IO /* backend FatFs / disk I/O failure */

/* Directory entry returned in shmem by READDIR */
typedef struct
{
    char name[56];   //  null-terminated UTF-8 string
    uint32_t size;   // file size in bytes
    uint8_t is_dir;  // 1 if directory, 0 if file
    uint8_t _pad[3]; // padding for alignment
} fat32_dirent_t;    /* 68 bytes, ~60 per 4096 page */

/* Stat result returned in shmem by STAT */
typedef struct
{
    uint32_t size;   // file size in bytes
    uint8_t is_dir;  // 1 if directory, 0 if file
    uint8_t _pad[3]; // padding for alignment
} fat32_stat_t;

_Static_assert(sizeof(fat32_dirent_t) <= 64, "dirent should stay cache-line-ish");

#define FAT32_SHM_SIZE 32768   /* must match the _shm_create size */
#define MAX_DIRENTS (FAT32_SHM_SIZE / sizeof(fat32_dirent_t))   /* 481 */

_Static_assert(MAX_DIRENTS * sizeof(fat32_dirent_t) <= FAT32_SHM_SIZE, "readdir overflow");

#ifdef __cplusplus
}
#endif

#endif