/* Spawns clients that attach to ttysvc and fsd and then die without closing,
 * far more of them than either server has slots, and checks that a fresh
 * client can still attach afterwards. */

#define UDBG_ENABLED 1

#include <dev/protocols/tty.h>
#include <fs/fsd_client.h>
#include <stdlib.h>
#include <string.h>
#include <util/spawn.h>
#include <zuzu/err.h>
#include <zuzu/service.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define ROUNDS 60
#define CHILD_QUIT_STATUS 10
#define CHILD_PATH "/bin/livechild"
#define JOIN_MS 5000

static int g_fail;
static FsdConn g_fsd;

static void Check(bool ok, const char *msg)
{
    if (ok)
        return;
    g_fail++;
    UserspaceDebugLog("livetest: FAIL: %s", msg);
}

static bool FsdUp(void)
{
    for (int i = 0; i < 200; i++)
    {
        if (FsdConnect(&g_fsd, FSD_SHM_DEFAULT) == ZUZU_OK)
            return true;
        Sleep(50);
    }
    return false;
}

static void *ReadFile(const char *path, size_t *len)
{
    FsdStat st;
    uint32_t fd;
    if (FsdGetStat(&g_fsd, path, &st) != ZUZU_OK || st.type != FSD_TYPE_FILE)
        return NULL;
    if (FsdOpen(&g_fsd, path, FSD_MODE_READ, &fd) != ZUZU_OK)
        return NULL;
    uint8_t *buf = malloc(st.size ? st.size : 1);
    size_t off = 0;
    while (buf && off < st.size)
    {
        uint32_t got = 0;
        if (FsdRead(&g_fsd, fd, buf + off, st.size - (uint32_t)off, &got) != ZUZU_OK || got == 0)
            break;
        off += got;
    }
    FsdClose(&g_fsd, fd);
    if (off != st.size)
    {
        free(buf);
        return NULL;
    }
    *len = off;
    return buf;
}

static bool StartChild(const void *image, size_t len, const char *mode, Spid *pid, Handle *task)
{
    char argbuf[32];
    size_t n = 0;
    memcpy(argbuf + n, "livechild", sizeof("livechild"));
    n += sizeof("livechild");
    size_t m = strlen(mode) + 1;
    memcpy(argbuf + n, mode, m);
    n += m;

    return SpawnProcess(image, len, "livechild", argbuf, n, 2, pid, task) == ZUZU_OK;
}

static bool RunChild(const void *image, size_t len, const char *mode, TaskWaitResult *out)
{
    Spid pid;
    Handle task;
    if (!StartChild(image, len, mode, &pid, &task))
        return false;
    *out = FormatToTaskWait(WaitOn(task, JOIN_MS));
    if (out->status != ZUZU_OK)
        TaskKill(task);
    HandleDestroy((Handle)pid);
    HandleClose(task);
    return true;
}

static void TestDyingClients(const void *image, size_t len)
{
    for (int i = 0; i < ROUNDS; i++)
    {
        bool fault = (i % 2) == 0;
        TaskWaitResult tw;
        if (!RunChild(image, len, fault ? "fault" : "quit", &tw))
        {
            Check(false, "spawn a dying client");
            continue;
        }
        if (fault)
            Check(tw.status == ZUZU_OK && tw.outcome == TASK_FAULTED,
                  "a client attached to ttysvc and fsd, then faulted");
        else
            Check(tw.status == ZUZU_OK && tw.outcome == TASK_EXITED && tw.value == CHILD_QUIT_STATUS,
                  "a client attached to ttysvc and fsd, then quit");
        Sleep(2);
    }
}

static void TestFreshClients(const void *image, size_t len)
{
    TaskWaitResult tw;
    Check(RunChild(image, len, "return", &tw) && tw.status == ZUZU_OK && tw.outcome == TASK_EXITED &&
              tw.value == 0,
          "a new client can attach to ttysvc and fsd and open a file");

    FsdConn extra;
    memset(&extra, 0, sizeof(extra));
    uint32_t fd;
    Check(FsdConnect(&extra, FSD_SHM_DEFAULT) == ZUZU_OK, "a second session attaches to fsd");
    Check(FsdOpen(&extra, "/README.md", FSD_MODE_READ, &fd) == ZUZU_OK, "and opens a file");
    FsdDetach(&extra);
}

#define SELFCALL_ROUNDS 40

static void ExpectServersServe(const void *image, size_t len, const char *what)
{
    TaskWaitResult tw;
    Check(RunChild(image, len, "return", &tw) && tw.status == ZUZU_OK && tw.outcome == TASK_EXITED &&
              tw.value == 0,
          what);
}

/* A client that Calls its own liveness port must not be mistaken for a
 * request by the server's liveness check, and must still be reaped. */
static void TestSelfCall(const void *image, size_t len, const char *mode)
{
    Spid pid;
    Handle task;

    Check(StartChild(image, len, mode, &pid, &task), "spawn a self-calling client");
    Sleep(150);
    TaskWaitResult tw;
    Check(RunChild(image, len, "quit", &tw), "a client dies while the self-caller blocks");
    Sleep(50);
    ExpectServersServe(image, len, "the servers still serve a new client with a self-caller blocked");
    Check(FormatToTaskWait(WaitOn(task, TIMEOUT_POLL)).status == ERR_TIMEOUT,
          "the self-caller is still blocked on its own port");
    HandleDestroy((Handle)pid);
    HandleClose(task);

    for (int i = 0; i < SELFCALL_ROUNDS; i++)
    {
        if (!StartChild(image, len, mode, &pid, &task))
        {
            Check(false, "spawn a self-calling client");
            continue;
        }
        Sleep(40);
        HandleDestroy((Handle)pid);
        HandleClose(task);
        Sleep(5);
    }
    ExpectServersServe(image, len, "self-callers killed in bulk were all reaped");
}

static void TestStaleFsdBadge(void)
{
    FsdConn c;
    memset(&c, 0, sizeof(c));
    Check(FsdConnect(&c, FSD_SHM_DEFAULT) == ZUZU_OK, "attach for the stale-badge check");
    SvcResult dup = HandleDuplicate(c.port, PERM_SEND, MARKER_NONE);
    Check((Err)dup.r0 == ZUZU_OK, "duplicate the badged handle");
    FsdDetach(&c);

    FsdRequest req;
    memset(&req, 0, sizeof(req));
    req.size = sizeof(req);
    req.cmd = FSD_CLOSE;
    req.data_off = FSD_DATA_OFF;
    memcpy(MessageBuf(), &req, sizeof(req));
    SvcResult r = Call((Handle)dup.r1, sizeof(req), -1);
    FsdResponse resp;
    memcpy(&resp, MessageBuf(), sizeof(resp));
    Check(r.r0 == ZUZU_OK && resp.status == ERR_NOTCONN, "the old fsd badge is rejected with NOTCONN");

    FsdConn again;
    memset(&again, 0, sizeof(again));
    Check(FsdConnect(&again, FSD_SHM_DEFAULT) == ZUZU_OK, "the slot is reusable");
    memcpy(MessageBuf(), &req, sizeof(req));
    r = Call((Handle)dup.r1, sizeof(req), -1);
    memcpy(&resp, MessageBuf(), sizeof(resp));
    Check(r.r0 == ZUZU_OK && resp.status == ERR_NOTCONN,
          "the old fsd badge is still rejected after its slot is reused");
    FsdDetach(&again);
    HandleClose((Handle)dup.r1);
}

static void TestStaleTtyIndex(void)
{
    Handle port = LookupService("/svc/tty");
    Handle ev = CreateEvent();
    Check(port >= 0 && ev >= 0, "resolve ttysvc");
    if (port < 0 || ev < 0)
        return;

    TtyConn c;
    Check(TtyClientConnect(port, TTY_ATTACH, "", ev, 0, &c) == ZUZU_OK, "attach for the stale-index check");
    uint32_t stale = c.index;
    TtyClientClose(port, &c);

    TtyCloseRequest req = { .cmd = TTY_CLOSE, .index = stale };
    Check(TtyCall(port, &req, sizeof(req), -1, NULL) == ERR_NOTCONN,
          "the old ttysvc session index is rejected with NOTCONN");

    TtyConn d;
    Check(TtyClientConnect(port, TTY_ATTACH, "", ev, 0, &d) == ZUZU_OK, "the slot is reusable");
    Check(d.index != stale, "the reused slot has a new generation");
    Check(TtyCall(port, &req, sizeof(req), -1, NULL) == ERR_NOTCONN,
          "the old ttysvc index is still rejected after its slot is reused");
    TtyClientClose(port, &d);
    HandleClose(ev);
    HandleClose(port);
}

int main(void)
{
    UserspaceDebugLog("livetest: start");
    if (!FsdUp())
    {
        UserspaceDebugLog("livetest: FAILED (fsd never came up)");
        return 1;
    }

    size_t len = 0;
    void *image = ReadFile(CHILD_PATH, &len);
    Check(image != NULL, "read " CHILD_PATH " from the SD card");
    if (image)
    {
        TestDyingClients(image, len);
        UserspaceDebugLog("livetest: dying clients done");
        TestFreshClients(image, len);
        TestSelfCall(image, len, "selfcall-fsd");
        UserspaceDebugLog("livetest: fsd self-call done");
        TestSelfCall(image, len, "selfcall-tty");
        free(image);
    }
    TestStaleFsdBadge();
    TestStaleTtyIndex();

    UserspaceDebugLog("livetest: %s (%d failures)", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
