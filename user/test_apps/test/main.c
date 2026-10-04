/* Memory-pressure event test: map anonymous memory until the kernel raises
 * EVENT_MEMMGMT, release it all, then check the event fires a second time. */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <zuzu/err.h>
#include <zuzu/syspage.h>
#include <zuzu/zuzu.h>

#define MAX_ITERS 256
#define CHUNK_BYTES (64 * 4096)
#define MEMMGMT_BIT (1u << 0)

static void *g_chunks[MAX_ITERS];

/* Maps chunks until the pressure bit fires; returns how many were mapped. */
static int FillUntilPressure(Handle ev, bool *fired)
{
    const Syspage *sp = SYSPAGE;
    *fired = false;
    int n;
    for (n = 0; n < MAX_ITERS; n++) {
        g_chunks[n] = MemMapAnon(CHUNK_BYTES, 0, PROT_RW);
        if (PtrIsErr(g_chunks[n])) {
            printf("MemMapAnon failed at iter %d (%d) before the event fired\n", n,
                   (int)(intptr_t)g_chunks[n]);
            break;
        }
        memset(g_chunks[n], 0, CHUNK_BYTES);

        EventWaitResult r = FormatToEventWait(WaitOn(ev, TIMEOUT_POLL));
        if (r.status == ZUZU_OK && (r.bits & MEMMGMT_BIT)) {
            printf("fired at iter %d, %u KiB free\n", n, (unsigned)sp->mem_free_kb);
            *fired = true;
            return n + 1;
        }
        if (n % 10 == 0)
            printf("iter %d: %u KiB free\n", n, (unsigned)sp->mem_free_kb);
    }
    return n;
}

static void ReleaseAll(int n)
{
    for (int i = 0; i < n; i++)
        MemUnmap(g_chunks[i]);
}

int main(void)
{
    const Syspage *sp = SYSPAGE;
    Handle ev = CreateEvent();
    if (ev < 0 || BindMemMgmt(ev) != ZUZU_OK) {
        printf("TEST FAILED: cannot subscribe to memory pressure\n");
        return 1;
    }
    printf("start: %u KiB free\n", (unsigned)sp->mem_free_kb);

    bool first, second;
    int n = FillUntilPressure(ev, &first);
    ReleaseAll(n);
    WaitOn(ev, TIMEOUT_POLL); /* drain bits left over from the first fire */

    n = FillUntilPressure(ev, &second);
    ReleaseAll(n);

    if (first && second)
        printf("TEST PASSED\n");
    else
        printf("TEST FAILED: first=%d second=%d\n", first, second);
    HandleClose(ev);
    return first && second ? 0 : 1;
}
