#ifndef SYSD_PROTOCOL_H
#define SYSD_PROTOCOL_H

#include <stdint.h>
#include <zuzu/types.h>

#define SYSD_EXEC 0xE0   // move out of the 1-9 range to avoid any accidental collision with _call command values

#define EXEC_OK       0
#define EXEC_ENOENT  (-1)    // path not found on fbox
#define EXEC_ENOMEM  (-2)    // alloc or asinject failed
#define EXEC_EBADELF (-3)    // ELF validation failed
#define EXEC_EIO     (-4)    // fbox read error
#define SYSD_GET_TTY 5

// Request layout in IPCX buffer (shell to sysd via _callx):
typedef struct {
    uint8_t  cmd;            // SYSD_EXEC
    uint8_t  _pad;
    uint16_t task_handle;    // slot in sysd's handle table (from _port_grant)
    uint16_t path_len;       // excluding NUL
    uint16_t argc;
    uint32_t pid;            // PID returned by caller's _tspawn
    // followed by char path[path_len + 1]  (NUL-terminated)
    // followed by char argbuf[...]          (NUL-delimited argv strings)
} exec_request_hdr_t;

// Reply layout in IPCX buffer (sysd to shell via _replyx):
typedef struct {
    uint32_t entry;          // ELF entry point
    uint32_t sp;             // user stack pointer after argv layout
    uint32_t argc;           // passed through
    uint32_t argv_va;        // pointer to argv array on user stack
    pid_t   pid;            // PID of the new process
} exec_reply_t;

#endif