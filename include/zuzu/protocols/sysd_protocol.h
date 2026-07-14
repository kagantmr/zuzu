#ifndef SYSD_PROTOCOL_H
#define SYSD_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zuzu/types.h>

#define SYSD_EXEC 0xE0   // move out of the 1-9 range to avoid any accidental collision with _call command values

// Exec status: success replies via the exec_reply_t struct; failures use
// err_t values from <zuzu/err.h> (ERR_NOENT for a missing path, ERR_NOMEM for
// alloc/asinject failure) plus two loader-specific codes below, which have no
// err_t equivalent and live outside the err_t range to avoid collisions.
#define EXEC_EBADELF (-100)  // ELF validation failed
#define EXEC_EIO     (-101)  // fbox read error

// Request layout in IPCX buffer (shell to sysd via _lcall):
typedef struct {
    uint8_t  cmd;            // SYSD_EXEC
    uint8_t  _pad;           // padding for alignment
    uint16_t task_handle;    // slot in sysd's handle table (from _port_grant)
    uint16_t path_len;       // excluding NUL
    uint16_t argc;           // number of argv strings
    uint32_t pid;            // PID returned by caller's _tspawn
    // followed by char path[path_len + 1]  (NUL-terminated)
    // followed by char argbuf[...]          (NUL-delimited argv strings)
} exec_request_hdr_t;

// Reply layout in IPCX buffer (sysd to shell via _lreply):
typedef struct {
    uint32_t entry;          // ELF entry point
    uint32_t sp;             // user stack pointer after argv layout
    uint32_t argc;           // passed through
    uint32_t argv_va;        // pointer to argv array on user stack
    zpid_t   pid;            // PID of the new process
} exec_reply_t;

#ifdef __cplusplus
}
#endif

#endif
