/**
 * @file stdio_redir.c
 *
 * @brief Finding and handing on a process's redirected stdin, stdout and stderr.
 */

#include <stdbool.h>
#include <util/stdio_redir.h>
#include <zuzu/zuzu.h>

/** @brief Perms the child gets on the page of a redirected end. */
#define REDIR_MEM_PERMS (PERM_MAP | PERM_WRITE)
/** @brief Perms the child gets on the event of a redirected end. */
#define REDIR_EVENT_PERMS (PERM_SEND | PERM_WAIT)

static PipeEnd g_in;   /**< The redirected stdin. */
static PipeEnd g_out;  /**< The redirected stdout. */
static PipeEnd g_err;  /**< The redirected stderr. */
static bool g_scanned; /**< Whether the slots have been looked at. */
static bool g_has_in;  /**< Whether stdin is redirected. */
static bool g_has_out; /**< Whether stdout is redirected. */
static bool g_has_err; /**< Whether stderr is redirected. */

/**
 * @brief Duplicates a handle, grants the copy to the space and drops the duplicate.
 *
 * @param[in] space Space handle to grant into.
 * @param[in] src   Handle to duplicate.
 * @param[in] perms Rights the copy gets.
 * @param[in] slot  The slot the granted handle must land in.
 * @return ZUZU_OK, or the first error; ERR_BUSY if the handle landed elsewhere.
 */
static Err GrantTo(Handle space, Handle src, HandlePerms perms, Handle slot)
{
    SvcResult dup = HandleDuplicate(src, perms | PERM_TXFR, MARKER_NONE);
    if (dup.r0 != ZUZU_OK)
        return (Err)dup.r0;
    Handle h = (Handle)dup.r1;

    SvcResult g = HandleGrant(h, space, perms);
    HandleClose(h);
    if (g.r0 != ZUZU_OK)
        return (Err)g.r0;
    return (Handle)g.r1 == slot ? ZUZU_OK : ERR_BUSY;
}

Err StdioRedirGrant(Handle space, const PipeEnd *in, const PipeEnd *out, const PipeEnd *err)
{
    if (!in && !out && !err)
        return ZUZU_OK;

    Handle none = CreateEvent();
    if (none < 0)
        return (Err)none;

    const PipeEnd *ends[3] = {in, out, err};
    Err rc = ZUZU_OK;
    for (int i = 0; i < 3 && rc == ZUZU_OK; i++) {
        Handle mem_slot = STDIO_SLOT_IN_MEM + (2 * i);
        if (ends[i] && ends[i]->shm) {
            rc = GrantTo(space, ends[i]->mem, REDIR_MEM_PERMS, mem_slot);
            if (rc == ZUZU_OK)
                rc = GrantTo(space, ends[i]->event, REDIR_EVENT_PERMS, mem_slot + 1);
        } else {
            rc = GrantTo(space, none, PERM_SEND, mem_slot);
            if (rc == ZUZU_OK)
                rc = GrantTo(space, none, PERM_SEND, mem_slot + 1);
        }
    }
    HandleClose(none);
    return rc;
}

/**
 * @brief Opens the end whose page and event sit in two slots, if they hold a real pipe.
 *
 * @param[out] e        The end.
 * @param[in]  role     PIPE_READER or PIPE_WRITER.
 * @param[in]  mem_slot Slot of the page; the event is the next slot.
 * @return true if the end was opened.
 */
static bool OpenSlots(PipeEnd *e, PipeRole role, Handle mem_slot)
{
    size_t bytes = 0;
    if (HandleQuerySize(mem_slot, &bytes) != ZUZU_OK)
        return false; /* a placeholder event, not a page */
    return OpenPipe(e, role, mem_slot, mem_slot + 1, PIPE_DEFAULT_DATA_BIT,
                    PIPE_DEFAULT_SPACE_BIT) == ZUZU_OK;
}

/** @brief Looks in the fixed slots for redirected ends and opens them. */
static void Scan(void)
{
    g_scanned = true;
    if (HandleQuery(STDIO_SLOT_IN_MEM, QUERY_TYPE).r0 != ZUZU_OK)
        return; /* nothing was granted: the usual case, one failed query */
    g_has_in = OpenSlots(&g_in, PIPE_READER, STDIO_SLOT_IN_MEM);
    g_has_out = OpenSlots(&g_out, PIPE_WRITER, STDIO_SLOT_OUT_MEM);
    g_has_err = OpenSlots(&g_err, PIPE_WRITER, STDIO_SLOT_ERR_MEM);
}

PipeEnd *StdioRedirIn(void)
{
    if (!g_scanned)
        Scan();
    return g_has_in ? &g_in : NULL;
}

PipeEnd *StdioRedirOut(void)
{
    if (!g_scanned)
        Scan();
    return g_has_out ? &g_out : NULL;
}

PipeEnd *StdioRedirErr(void)
{
    if (!g_scanned)
        Scan();
    return g_has_err ? &g_err : NULL;
}

void StdioRedirClose(void)
{
    if (!g_scanned)
        return;
    if (g_has_in) {
        ClosePipe(&g_in);
        g_has_in = false;
    }
    if (g_has_out) {
        ClosePipe(&g_out);
        g_has_out = false;
    }
    if (g_has_err) {
        ClosePipe(&g_err);
        g_has_err = false;
    }
}
