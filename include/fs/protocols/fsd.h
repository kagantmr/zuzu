/**
 * fsd.h - fs daemon protocol information
 *
 * fsd (fs daemon) is the zuzuOS VFS. It talks to a block driver through a
 * filesystem backend shim and serves clients over a port registered as
 * "/svc/fsd".
 *
 * Sessions. A caller reaches fsd on an unmarked handle (from LookupService),
 * which may only send FSD_ATTACH, granting a shared page it created (the
 * client pays for it; the page size rides in FsdRequest.data_len). fsd
 * allocates a session, maps the page and replies granting a PERM_SEND|PERM_TXFR
 * duplicate of its own port stamped with a marker, FSD_BADGE(generation, slot).
 * Every later request goes on that badged handle; fsd reads the marker from
 * the receive result and rejects anything that does not match a live session,
 * so a session cannot be guessed or forged. FSD_DETACH frees the session; so
 * does the death of the client, once it has sent FSD_WATCH with a port it owns.
 * fds are per session.
 *
 * Requests. The payload is an FsdRequest in the message buffer. Paths and
 * file data live in the shared page at data_off >= FSD_DATA_OFF; fsd checks
 * data_off/data_len against the page and copies a path out before using it.
 * The reply payload is an FsdResponse in the message buffer; READ, STAT,
 * READDIR results are placed in the shared page at resp.data_off.
 */

#ifndef ZUZUOS_FSD_PROTOCOL_H
#define ZUZUOS_FSD_PROTOCOL_H

#include <types.h>
#include <zuzu/err.h>

typedef struct {
    uint32_t size; /* sizeof(FsdRequest); client sets, fsd honors */
    uint32_t cmd;
    uint32_t data_off; /* byte offset into shm where payload begins */
    uint32_t data_len;
    int64_t offset; /* SEEK naturally 8-byte aligned here */
    uint32_t fd;
    uint32_t whence; /* SEEK */
    uint32_t mode;   /* OPEN */
    uint32_t flags;
} FsdRequest;

_Static_assert(sizeof(FsdRequest) == 40, "FsdRequest layout changed");

typedef struct {
    uint32_t size;     /* sizeof(FsdResponse); fsd sets */
    Err status;        /* ZUZU_OK or Err */
    uint32_t data_off; /* where fsd put the payload */
    uint32_t data_len; /* how much */
    int64_t offset;    /* SEEK: new absolute position */
    uint32_t count;    /* READ/WRITE bytes, READDIR entries */
    uint32_t fd;       /* OPEN */
    uint32_t flags;
    uint32_t _rsv;
} FsdResponse;

_Static_assert(sizeof(FsdResponse) == 40, "FsdResponse layout changed");

typedef enum { FSD_SEEK_SET = 0, FSD_SEEK_CUR = 1, FSD_SEEK_END = 2 } FsdWhence;

typedef enum { FSD_TYPE_FILE = 0, FSD_TYPE_DIR = 1, FSD_TYPE_SYMLINK = 2 } FsdFileType;

typedef struct {
    char name[56];   //  null-terminated UTF-8 string
    uint32_t size;   // file size in bytes
    uint8_t type;    /* FsdFileType value */
    uint8_t _pad[3]; // padding for alignment
} FsdDirEntry;       /* 64 bytes */

/* Stat result returned in shmem by STAT */
typedef struct {
    uint32_t size;   // file size in bytes
    uint8_t type;    /* FsdFileType value */
    uint8_t _pad[3]; // padding for alignment
} FsdStat;

_Static_assert(sizeof(FsdDirEntry) <= 64, "dirent should stay cache-line-ish");

typedef enum {
    FSD_ATTACH = 1, /* unmarked: grant shm, data_len = size -> badged port */
    FSD_OPEN,       /* shm: path        -> fd                    */
    FSD_CLOSE,      /* fd                                        */
    FSD_READ,       /* fd, data_len = count -> count, data in shm  */
    FSD_WRITE,      /* fd, data_len = count, data in shm -> count  */
    FSD_SEEK,       /* fd, offset, whence -> new offset          */
    FSD_STAT,       /* shm: path -> stat struct in shm           */
    FSD_FSTAT,      /* fd        -> stat struct in shm           */
    FSD_READDIR,    /* shm: path -> dirents in shm, count        */
    FSD_UNLINK,     /* shm: path                                 */
    FSD_RENAME,     /* shm: two paths                            */
    FSD_DETACH,     /* free the session                          */
    FSD_WATCH, /* badged; grant: a port the client owns (PERM_WAIT|PERM_TXFR). fsd frees the session
                  when it dies */
} FsdCommand;

#define FSD_DATA_OFF 128u /* payload starts here; data_off >= FSD_DATA_OFF */
#define FSD_PAGE_SIZE 4096u
#define FSD_SHM_MIN 4096u           /* smallest buffer a client may grant */
#define FSD_SHM_DEFAULT (64 * 1024) /* suggested size; client chooses, fsd enforces MIN/MAX */
#define FSD_SHM_MAX (4 * 1024 * 1024)

#define FSD_BADGE(gen, slot) ((((uint32_t)(gen)) << 8) | ((uint32_t)(slot) & 0xFFu))
#define FSD_BADGE_SLOT(badge) ((uint32_t)(badge) & 0xFFu)
#define FSD_BADGE_GEN(badge) ((uint32_t)(badge) >> 8)

#define FSD_MODE_READ 0x01          /* FA_READ */
#define FSD_MODE_WRITE 0x02         /* FA_WRITE */
#define FSD_MODE_CREATE_NEW 0x04    /* fails if exists */
#define FSD_MODE_CREATE_ALWAYS 0x08 /* create or truncate  -> O_CREAT|O_TRUNC */
#define FSD_MODE_OPEN_ALWAYS 0x10   /* create if missing   -> O_CREAT */
#define FSD_MODE_OPEN_APPEND 0x30   /* -> O_APPEND */

#endif // ZUZUOS_FSD_PROTOCOL_H