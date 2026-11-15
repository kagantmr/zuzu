/**
 * @file stdio_redir.h
 *
 * @brief A parent can point a child's stdin and stdout at pipes. The child's libc looks the ends
 * up here on first use and falls back to the console when there are none.
 */

#ifndef UTIL_STDIO_REDIR_H
#define UTIL_STDIO_REDIR_H

#include <util/pipe.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Child handle slot of a redirected stdin's page. Slot 0 is the name service. */
#define STDIO_SLOT_IN_MEM 1
/** @brief Child handle slot of a redirected stdin's event. */
#define STDIO_SLOT_IN_EVENT 2
/** @brief Child handle slot of a redirected stdout's page. */
#define STDIO_SLOT_OUT_MEM 3
/** @brief Child handle slot of a redirected stdout's event. */
#define STDIO_SLOT_OUT_EVENT 4
/** @brief Child handle slot of a redirected stderr's page. */
#define STDIO_SLOT_ERR_MEM 5
/** @brief Child handle slot of a redirected stderr's event. */
#define STDIO_SLOT_ERR_EVENT 6

/**
 * @brief Gives a new space a redirected stdin and/or stdout.
 *
 * Grants six handles to @p space, to the fixed slots STDIO_SLOT_*: a page and an event for each of
 * stdin, stdout and stderr. A direction without a pipe gets a placeholder event in both of its
 * slots, so the others stay where they are. Call it right after the name service handle is
 * granted, before the task starts; with no pipes at all it grants nothing.
 *
 * @param[in] space  Space handle with PERM_CNTL.
 * @param[in] in     The pipe's reader end for stdin, or NULL.
 * @param[in] out    The pipe's writer end for stdout, or NULL.
 * @param[in] err    The pipe's writer end for stderr, or NULL.
 * @return ZUZU_OK, or the first error; ERR_BUSY if a handle did not land in its slot.
 */
Err StdioRedirGrant(Handle space, const PipeEnd *in, const PipeEnd *out, const PipeEnd *err);

/**
 * @brief The pipe this process reads as stdin.
 *
 * @return The pipe, or NULL if the parent handed none.
 */
PipeEnd *StdioRedirIn(void);

/**
 * @brief The pipe this process writes as stdout.
 *
 * @return The pipe, or NULL if the parent handed none.
 */
PipeEnd *StdioRedirOut(void);

/**
 * @brief The pipe this process writes as stderr.
 *
 * @return The pipe, or NULL if the parent handed none.
 */
PipeEnd *StdioRedirErr(void);

/** @brief Marks this process's redirected ends closed, so the peer sees EOF or a broken pipe. */
void StdioRedirClose(void);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_STDIO_REDIR_H */
