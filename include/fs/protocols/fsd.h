/**
 * @file
 * @brief fsd's protocol: sessions, requests and the shared buffer.
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

/** @brief A request, in the message buffer. */
typedef struct {
    uint32_t size;     /**< sizeof(FsdRequest); the client sets it, fsd checks it. */
    uint32_t cmd;      /**< An FsdCommand. */
    uint32_t data_off; /**< Offset of the payload in the shared buffer, at least FSD_DATA_OFF. */
    uint32_t data_len; /**< Payload length, or the byte count for READ and WRITE. */
    int64_t offset;    /**< SEEK offset, or the first entry for READDIR. */
    uint32_t fd;       /**< File descriptor, for commands on an open file. */
    uint32_t whence;   /**< SEEK: an FsdWhence. */
    uint32_t mode;     /**< OPEN: FSD_MODE_* flags. */
    uint32_t flags;    /**< Reserved; 0. */
} FsdRequest;

_Static_assert(sizeof(FsdRequest) == 40, "FsdRequest layout changed");

/** @brief A reply, in the message buffer. */
typedef struct {
    uint32_t size;     /**< sizeof(FsdResponse); fsd sets it. */
    Err status;        /**< ZUZU_OK, or an error code. */
    uint32_t data_off; /**< Where fsd put the payload in the shared buffer. */
    uint32_t data_len; /**< Payload length. */
    int64_t offset;    /**< SEEK: the new absolute position. */
    uint32_t count;    /**< Bytes for READ and WRITE, entries for READDIR. */
    uint32_t fd;       /**< OPEN: the new file descriptor. */
    uint32_t flags;    /**< Reserved. */
    uint32_t _rsv;     /**< Reserved. */
} FsdResponse;

_Static_assert(sizeof(FsdResponse) == 40, "FsdResponse layout changed");

/** @brief What a SEEK offset is relative to. */
typedef enum {
    FSD_SEEK_SET = 0, /**< The start of the file. */
    FSD_SEEK_CUR = 1, /**< The current position. */
    FSD_SEEK_END = 2  /**< The end of the file. */
} FsdWhence;

/** @brief Kind of a directory entry. */
typedef enum {
    FSD_TYPE_FILE = 0,   /**< Regular file. */
    FSD_TYPE_DIR = 1,    /**< Directory. */
    FSD_TYPE_SYMLINK = 2 /**< Symbolic link. */
} FsdFileType;

/** @brief One READDIR entry in the shared buffer; 64 bytes. */
typedef struct {
    char name[56];   /**< NUL-terminated UTF-8 name. */
    uint32_t size;   /**< File size in bytes. */
    uint8_t type;    /**< An FsdFileType. */
    uint8_t _pad[3]; /**< Padding. */
} FsdDirEntry;

/** @brief STAT and FSTAT result in the shared buffer. */
typedef struct {
    uint32_t size;   /**< File size in bytes. */
    uint8_t type;    /**< An FsdFileType. */
    uint8_t _pad[3]; /**< Padding. */
    uint32_t mtime;  /**< FAT date << 16 | FAT time; 0 when the backend cannot say. */
} FsdStat;

_Static_assert(sizeof(FsdDirEntry) <= 64, "dirent should stay cache-line-ish");

/** @brief Request commands. */
typedef enum {
    /** On the unmarked handle: grants the shared buffer, data_len = its size; replies with the
     *  session's badged port. */
    FSD_ATTACH = 1,
    FSD_OPEN,    /**< Path in the buffer, mode; replies fd. */
    FSD_CLOSE,   /**< fd. */
    FSD_READ,    /**< fd, data_len = count; replies count, data in the buffer. */
    FSD_WRITE,   /**< fd, data_len = count, data in the buffer; replies count. */
    FSD_SEEK,    /**< fd, offset, whence; replies the new offset. */
    FSD_STAT,    /**< Path in the buffer; replies an FsdStat in the buffer. */
    FSD_FSTAT,   /**< fd; replies an FsdStat in the buffer. */
    /** Path in the buffer, offset = first entry; replies FsdDirEntry records and count. */
    FSD_READDIR,
    FSD_UNLINK,  /**< Path in the buffer. */
    FSD_RENAME,  /**< Two paths in the buffer. */
    FSD_DETACH,  /**< Ends the session. */
    /**
     * Grants a port the client owns (PERM_WAIT | PERM_TXFR); fsd frees the session when it dies.
     */
    FSD_WATCH,
    /** Grants a memory object (PERM_MAP | PERM_WRITE | PERM_TXFR); fd, data_off = offset into the
     *  object, data_len = count. Replies count; the data lands in the object. */
    FSD_READ_OBJ,
} FsdCommand;

/** @brief Payloads start at or after this offset in the shared buffer. */
#define FSD_DATA_OFF 128u
/** @brief Shared buffers are sized in pages of this many bytes. */
#define FSD_PAGE_SIZE 4096u
/** @brief Smallest shared buffer fsd accepts. */
#define FSD_SHM_MIN 4096u
/** @brief Suggested shared buffer size. */
#define FSD_SHM_DEFAULT (64 * 1024)
/** @brief Largest shared buffer fsd accepts. */
#define FSD_SHM_MAX (4 * 1024 * 1024)

/** @brief Session marker: generation above bit 8, session slot below. */
#define FSD_BADGE(gen, slot) ((((uint32_t)(gen)) << 8) | ((uint32_t)(slot) & 0xFFu))
/** @brief Session slot of a marker. */
#define FSD_BADGE_SLOT(badge) ((uint32_t)(badge) & 0xFFu)
/** @brief Session generation of a marker. */
#define FSD_BADGE_GEN(badge) ((uint32_t)(badge) >> 8)

/** @brief OPEN: read access. */
#define FSD_MODE_READ 0x01
/** @brief OPEN: write access. */
#define FSD_MODE_WRITE 0x02
/** @brief OPEN: create the file; fail if it exists. */
#define FSD_MODE_CREATE_NEW 0x04
/** @brief OPEN: create the file, or truncate it if it exists (O_CREAT | O_TRUNC). */
#define FSD_MODE_CREATE_ALWAYS 0x08
/** @brief OPEN: create the file if it is missing (O_CREAT). */
#define FSD_MODE_OPEN_ALWAYS 0x10
/** @brief OPEN: open or create, positioned at the end (O_APPEND). */
#define FSD_MODE_OPEN_APPEND 0x30

#endif // ZUZUOS_FSD_PROTOCOL_H