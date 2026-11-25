/**
 * @file stdio_core.h
 *
 * @brief Standard input, output and error as pipes.
 *
 * A process reads fd 0 from a pipe and writes fds 1 and 2 to pipes. Where they lead is up to
 * whoever started it: a parent can hand over pipes (SpawnAttr, see stdio_redir.h); with none, the
 * process opens a session with the terminal service (ttysvc) and its three rings are the pipes.
 * Nothing else in libc knows the difference.
 */

#ifndef UTIL_STDIO_CORE_H
#define UTIL_STDIO_CORE_H

#include <dev/tty.h>
#include <stdbool.h>
#include <stddef.h>
#include <util/pipe.h>
#include <util/stdio_redir.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The pipe behind fd 0 (stdin), 1 (stdout) or 2 (stderr).
 *
 * Opens the terminal session on first use when the parent handed over no pipe for @p fd.
 *
 * @param[in] fd 0, 1 or 2.
 * @return The pipe, or NULL if there is none (no terminal service).
 */
PipeEnd *StdioStream(int fd);

/**
 * @brief Whether @p fd is a terminal rather than a redirected pipe.
 *
 * @param[in] fd 0, 1 or 2.
 * @return true for a terminal session.
 */
bool StdioIsTty(int fd);

/**
 * @brief Reads from stdin, waiting for at least one byte.
 *
 * On a terminal it takes the foreground first. ^C ends the program (or runs the handler).
 *
 * @param[out] buf Where to store the bytes.
 * @param[in]  cap Room in @p buf.
 * @return Bytes read; 0 at end of input (^D, or the writer closed); a negative Err otherwise.
 */
int StdioRead(void *buf, size_t cap);

/**
 * @brief Writes all of @p len bytes to stdout or stderr, waiting for room.
 *
 * @param[in] fd  1 or 2.
 * @param[in] buf Bytes to send.
 * @param[in] len How many.
 * @return @p len, or a negative Err (ERR_DEAD for a broken pipe).
 */
int StdioWrite(int fd, const void *buf, size_t len);

/**
 * @brief Connects to the terminal service now, rather than on first use.
 *
 * @return 0, or -1 if there is no terminal service.
 */
int StdioOpenTty(void);

/** @brief Flushes and leaves the terminal session so another program can take the foreground. */
void StdioCloseTty(void);

/**
 * @brief Moves the terminal session to another endpoint.
 *
 * @param[in] alias Endpoint name; "" is the default.
 * @return 0, or -1.
 */
int StdioRouteTty(const char *alias);

/**
 * @brief Chooses the endpoint the first terminal session will attach to, without connecting.
 *
 * @param[in] alias Endpoint name; "" is the default. Ignored if it is too long.
 */
void StdioSetTtyAlias(const char *alias);

/**
 * @brief The endpoint the terminal session attaches to; once connected, the one it is on.
 *
 * @return The alias, "" while the default has not been resolved.
 */
const char *StdioTtyAlias(void);

/**
 * @brief Sets the terminal mode; it survives a reconnect.
 *
 * @param[in] flags TTY_MODE_* flags.
 * @return 0, or -1.
 */
int StdioSetMode(uint32_t flags);

/**
 * @brief Selects raw or cooked-with-echo mode.
 *
 * @param[in] enable Non-zero for raw.
 * @return 0, or -1.
 */
int StdioSetRaw(int enable);

/**
 * @brief The mode the terminal session is meant to be in.
 *
 * @return TTY_MODE_* flags.
 */
uint32_t StdioMode(void);

/**
 * @brief The terminal session.
 *
 * @return The session, or NULL if there is none.
 */
TtyConn *StdioTtyConn(void);

/**
 * @brief Handle to ttysvc.
 *
 * @return The handle, or -1 if there is no terminal session.
 */
Handle StdioTtyPort(void);

/**
 * @brief Sets what ^C does.
 *
 * @param[in] handler Runs in the program's thread; NULL ends the program.
 */
void StdioOnInterrupt(void (*handler)(void));

/** @brief Flushes and leaves the terminal and tells the peers of any redirected pipes. */
void StdioFini(void);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_STDIO_CORE_H */
