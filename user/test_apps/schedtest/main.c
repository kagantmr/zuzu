#define UDBG_ENABLED 1

#include <string.h>
#include <zuzu/err.h>
#include <zuzu/udbg.h>
#include <zuzu/user_layout.h>
#include <zuzu/zuzu.h>

#define ROUNDS 40

static const uint32_t kCodeLoop[] = { 0xEAFFFFFE }; /* b . */

/* Calls the port whose handle and xlen sit in the literal pool, then quits 7. */
static const uint32_t kCodeCallPort[] = {
    0xE59F0018, /* ldr r0, [pc, #24]   ; port handle  */
    0xE59F1018, /* ldr r1, [pc, #24]   ; xlen         */
    0xE3E02000, /* mvn r2, #0          ; no grant     */
    0xE3A03000, /* mov r3, #0                         */
    0xEF000005, /* svc SVC_CALL                       */
    0xE3A00007, /* mov r0, #7                         */
    0xEF000000, /* svc SVC_QUIT                       */
    0xE1A00000, /* nop                                */
    0, 0,       /* literal pool: handle, xlen         */
};
#define CALL_CODE_HANDLE_WORD 8
#define CALL_CODE_XLEN_WORD 9
#define CALL_SENDER_STATUS 7

static uint8_t g_code_page[4096] __attribute__((aligned(4096)));
static int g_fail;

typedef struct
{
    Handle space;
    Handle task;
} Kitten;

static void Check(bool ok, const char *msg)
{
    if (ok)
        return;
    g_fail++;
    UserspaceDebugLog("schedtest: FAIL: %s", msg);
}

static bool KittenInject(Kitten *k, const uint32_t *code, size_t bytes)
{
    memset(g_code_page, 0, sizeof(g_code_page));
    memcpy(g_code_page, code, bytes);
    return MemInject(k->space, USER_ELF_BASE, g_code_page, sizeof(g_code_page),
                     PROT_READ | PROT_EXEC, 0) == ZUZU_OK;
}

static bool KittenLaunch(Kitten *k)
{
    k->task = CreateTask(k->space);
    if (k->task < 0)
        return false;
    return TaskStart(k->task, (void *)USER_ELF_BASE, (void *)USR_SP, 0, 0) == ZUZU_OK;
}

static bool SpinnerStart(Kitten *k)
{
    k->space = CreateSpace("spinner");
    k->task = -1;
    if (k->space < 0)
        return false;
    return KittenInject(k, kCodeLoop, sizeof(kCodeLoop)) && KittenLaunch(k);
}

static void SpinnerFree(Kitten *k)
{
    if (k->space >= 0)
        HandleDestroy(k->space);
    if (k->task >= 0)
        HandleClose(k->task);
    k->space = k->task = -1;
}

static void TestSpinnerDestroy(void)
{
    for (int i = 0; i < ROUNDS; i++)
    {
        Kitten k;
        Check(SpinnerStart(&k), "spinner starts");
        if (i % 2)
            Yield();
        SpinnerFree(&k);
    }
}

static void TestPriorityOnQueued(void)
{
    static const uint32_t prios[] = { 0, 1, 0, 0, 1, 0 };
    int applied = 0;

    for (int i = 0; i < ROUNDS; i++)
    {
        Kitten k;
        Check(SpinnerStart(&k), "spinner starts");

        uint32_t p = prios[i % (sizeof(prios) / sizeof(prios[0]))];
        Err prc = TaskSetPriority(k.task, p);
        Check(prc == ZUZU_OK, "SetPriority on a queued spinner returns OK");
        if (prc == ZUZU_OK)
            applied++;
        if (i % 3 == 1)
            Yield();
        if (i % 3 == 2)
            Sleep(2);

        if (i % 2)
            TaskKill(k.task);
        SpinnerFree(&k);
    }
    Check(applied > 0, "SetPriority on a queued task succeeds at least once");
}

static void TestTwoQueued(void)
{
    for (int i = 0; i < ROUNDS; i++)
    {
        Kitten a, b;
        Check(SpinnerStart(&a), "first spinner starts");
        Check(SpinnerStart(&b), "second spinner starts");
        Check(TaskSetPriority(a.task, 0) == ZUZU_OK, "SetPriority(0) on a queued spinner");
        Check(TaskSetPriority(b.task, 1) == ZUZU_OK, "SetPriority(1) on a queued spinner");
        SpinnerFree(&a);
        Yield();
        SpinnerFree(&b);
    }
}

static bool SenderStart(Kitten *k, Handle port, Marker marker, uint32_t xlen)
{
    k->space = CreateSpace("sender");
    k->task = -1;
    if (k->space < 0)
        return false;
    SvcResult dup = HandleDuplicate(port, PERM_SEND, marker);
    if ((Err)dup.r0 != ZUZU_OK)
        return false;
    SvcResult gr = HandleGrant((Handle)dup.r1, k->space, PERM_SEND);
    HandleClose((Handle)dup.r1);
    if ((Err)gr.r0 != ZUZU_OK)
        return false;

    uint32_t code[sizeof(kCodeCallPort) / sizeof(kCodeCallPort[0])];
    memcpy(code, kCodeCallPort, sizeof(code));
    code[CALL_CODE_HANDLE_WORD] = (uint32_t)gr.r1;
    code[CALL_CODE_XLEN_WORD] = xlen;
    return KittenInject(k, code, sizeof(code)) && KittenLaunch(k);
}

static bool ReceiveOne(Handle port, Marker want_marker, uint32_t want_xlen, const char *what)
{
    PortWaitResult r = FormatToPortWait(WaitOn(port, 1000));
    bool ok = r.status == ZUZU_OK && r.sender == want_marker && r.xlen == want_xlen;
    Check(ok, what);
    return r.status == ZUZU_OK;
}

static void ExpectSenderDone(Kitten *k, const char *what)
{
    TaskWaitResult tw = FormatToTaskWait(WaitOn(k->task, 1000));
    Check(tw.status == ZUZU_OK && tw.outcome == TASK_EXITED && tw.value == CALL_SENDER_STATUS, what);
}

static void TestPriorityOnBlockedSender(void)
{
    Handle port = CreatePort();
    Check(port >= 0, "create the port");

    for (int i = 0; i < ROUNDS; i++)
    {
        Kitten a, b;

        Check(SenderStart(&a, port, 0xA0, 4), "first sender starts");
        Sleep(3);
        Check(TaskSetPriority(a.task, 0) == ZUZU_OK, "SetPriority on a blocked sender");
        Check(TaskSetPriority(a.task, 1) == ZUZU_OK, "SetPriority back on a blocked sender");
        if (ReceiveOne(port, 0xA0, 4, "the call arrives intact after SetPriority"))
        {
            Check(Reply(0, -1) == ZUZU_OK, "Reply to the sender");
            ExpectSenderDone(&a, "Reply wakes the sender");
        }
        SpinnerFree(&a);

        Check(SenderStart(&a, port, 0xB0, 8), "first of two senders starts");
        Sleep(3);
        Check(SenderStart(&b, port, 0xB1, 12), "second of two senders starts");
        Sleep(3);
        Check(TaskSetPriority(b.task, 0) == ZUZU_OK, "SetPriority on the second blocked sender");
        Check(TaskSetPriority(a.task, 0) == ZUZU_OK, "SetPriority on the first blocked sender");
        Check(TaskSetPriority(b.task, 1) == ZUZU_OK, "SetPriority back on the second sender");
        Check(TaskSetPriority(a.task, 1) == ZUZU_OK, "SetPriority back on the first sender");

        if (ReceiveOne(port, 0xB0, 8, "the first sender arrives first"))
        {
            Check(Reply(0, -1) == ZUZU_OK, "Reply to the first sender");
            ExpectSenderDone(&a, "Reply wakes the first sender");
        }
        if (ReceiveOne(port, 0xB1, 12, "the second sender arrives second"))
        {
            Check(Reply(0, -1) == ZUZU_OK, "Reply to the second sender");
            ExpectSenderDone(&b, "Reply wakes the second sender");
        }
        SpinnerFree(&a);
        SpinnerFree(&b);
    }
    HandleClose(port);
}

#define WITNESSES 3

static Kitten g_witness[WITNESSES];

static void WitnessesStart(void)
{
    for (int i = 0; i < WITNESSES; i++)
        Check(SpinnerStart(&g_witness[i]), "witness spinner starts");
}

static void WitnessesVerify(void)
{
    for (int i = 0; i < WITNESSES; i++)
    {
        TaskWaitResult tw = FormatToTaskWait(WaitOn(g_witness[i].task, TIMEOUT_POLL));
        Check(tw.status == ERR_TIMEOUT, "witness spinner is still running (neither exited nor faulted)");
        SpinnerFree(&g_witness[i]);
    }
}

int main(void)
{
    UserspaceDebugLog("schedtest: start");
    WitnessesStart();
    TestSpinnerDestroy();
    UserspaceDebugLog("schedtest: spinner destroy done");
    TestPriorityOnQueued();
    UserspaceDebugLog("schedtest: priority on queued done");
    TestTwoQueued();
    UserspaceDebugLog("schedtest: two queued done");
    TestPriorityOnBlockedSender();
    UserspaceDebugLog("schedtest: blocked sender done");

    Check(Sleep(20) == ZUZU_OK, "scheduler still runs after the stress");
    WitnessesVerify();
    UserspaceDebugLog("schedtest: %s (%d failures)", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
