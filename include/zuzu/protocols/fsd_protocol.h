/**
 * fsd_protocol.h - fs daemon protocol information
 *
 * fsd (fs daemon) is the zuzuOS v0.6 VFS, replacing the fat32d-fbox chain. Its job is to talk to storage drivers and
 * with addition of new filesystem shims, it can handle multiple file types.
 *
 * The protocol is mainly over msg (no lmsg) with msg_call(), optionally there are request and response structs in shm that
 * the user/fsd will be asked to refer to if the reply/request is too long (passing it over lmsg is slow).
 *
 * Request:
 * - r2: cmd (reads request struct for OPEN, SEEK, STAT, READDIR, UNLINK, RENAME, must match that cmd field or is rejected)
 * - r3: arg (fd or fd|count << 16) for registers
 * Response:
 * - r1: status
 * - r2: primary result (read struct for extras)
 *
 * Client must create and grant the shm. The calling process is charged for the memory, not fsd. To prevent TOCTOU,
 * fsd copies the request out of shm before acting on it. fsd also validates data_off >= FSD_DATA_OFF, data_len <= shm_size - data_off and rejects
 * if this is violated. fds are per-client, fsd keys them by sender PID.
 */

#ifndef ZUZUOS_FSD_PROTOCOL_H
#define ZUZUOS_FSD_PROTOCOL_H

#include <zuzu/err.h>
#include <zuzu/types.h>

typedef struct
{
    uint32_t size; /* sizeof(fsd_req_t); client sets, fsd honors */
    uint32_t cmd;
    uint32_t data_off; /* byte offset into shm where payload begins */
    uint32_t data_len;
    int64_t offset; /* SEEK naturally 8-byte aligned here */
    uint32_t fd;
    uint32_t whence; /* SEEK */
    uint32_t mode;   /* OPEN */
    uint32_t flags;
} fsd_req_t;

_Static_assert(sizeof(fsd_req_t) == 40, "fsd_req_t layout changed");

typedef struct
{
    uint32_t size;     /* sizeof(fsd_resp_t); fsd sets */
    err_t status;      /* ZUZU_OK or err_t */
    uint32_t data_off; /* where fsd put the payload */
    uint32_t data_len; /* how much */
    int64_t offset;    /* SEEK: new absolute position */
    uint32_t count;    /* READ/WRITE bytes, READDIR entries */
    uint32_t fd;       /* OPEN */
    uint32_t flags;
    uint32_t _rsv;
} fsd_resp_t;

_Static_assert(sizeof(fsd_resp_t) == 40, "fsd_resp_t layout changed");

typedef enum { FSD_SEEK_SET = 0, FSD_SEEK_CUR = 1, FSD_SEEK_END = 2 } fsd_whence_t;
typedef enum { FSD_TYPE_FILE = 0, FSD_TYPE_DIR = 1, FSD_TYPE_SYMLINK = 2 } fsd_ftype_t;

typedef struct
{
    char name[56];   //  null-terminated UTF-8 string
    uint32_t size;   // file size in bytes
    uint8_t type;    /* fsd_ftype_t value */
    uint8_t _pad[3]; // padding for alignment
} fsd_dirent_t;       /* 64 bytes */

/* Stat result returned in shmem by STAT */
typedef struct
{
    uint32_t size;   // file size in bytes
    uint8_t type;    /* fsd_ftype_t value */
    uint8_t _pad[3]; // padding for alignment
} fsd_stat_t;

_Static_assert(sizeof(fsd_dirent_t) <= 64, "dirent should stay cache-line-ish");

typedef enum
{
    FSD_SET_BUF = 1, /* client grants its shm; fsd maps it        */
    FSD_OPEN,        /* shm: path        -> fd                    */
    FSD_CLOSE,       /* reg: fd                                   */
    FSD_READ,        /* reg: fd|count<<16 -> count, data in shm   */
    FSD_WRITE,       /* reg: fd|count<<16, data in shm -> count   */
    FSD_SEEK,        /* shm: offset+whence -> new offset          */
    FSD_STAT,        /* shm: path -> stat struct in shm           */
    FSD_FSTAT,       /* reg: fd   -> stat struct in shm           */
    FSD_READDIR,     /* shm: path -> dirents in shm, count        */
    FSD_UNLINK,      /* shm: path                                 */
    FSD_RENAME,      /* shm: two paths                            */
} fsd_cmd_t;

#define FSD_REQ_OFF 0u
#define FSD_RESP_OFF 64u
#define FSD_DATA_OFF 128u           /* payload starts here; data_off >= FSD_DATA_OFF */
#define FSD_SHM_MIN 4096u           /* smallest buffer a client may grant */
#define FSD_SHM_DEFAULT (64 * 1024) /* suggested size; client chooses, fsd enforces MIN/MAX */
#define FSD_SHM_MAX (4 * 1024 * 1024)

#define FSD_MODE_READ 0x01          /* FA_READ */
#define FSD_MODE_WRITE 0x02         /* FA_WRITE */
#define FSD_MODE_CREATE_NEW 0x04    /* fails if exists */
#define FSD_MODE_CREATE_ALWAYS 0x08 /* create or truncate  -> O_CREAT|O_TRUNC */
#define FSD_MODE_OPEN_ALWAYS 0x10   /* create if missing   -> O_CREAT */
#define FSD_MODE_OPEN_APPEND 0x30   /* -> O_APPEND */

#endif // ZUZUOS_FSD_PROTOCOL_H