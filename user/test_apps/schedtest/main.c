#define UDBG_ENABLED 1

#include <string.h>
#include <zuzu/err.h>
#include <zuzu/udbg.h>
#include <zuzu/user_layout.h>
#include <zuzu/zuzu.h>

#define ROUNDS 40

static const uint32_t kCodeLoop[] = { 0xEAFFFFFE }; /* b . */

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

static bool SpinnerStart(Kitten *k)
{
    k->space = CreateSpace("spinner");
    k->task = -1;
    if (k->space < 0)
        return false;
    memset(g_code_page, 0, sizeof(g_code_page));
    memcpy(g_code_page, kCodeLoop, sizeof(kCodeLoop));
    if (MemInject(k->space, USER_ELF_BASE, g_code_page, sizeof(g_code_page),
                  PROT_READ | PROT_EXEC, 0) != ZUZU_OK)
        return false;
    k->task = CreateTask(k->space);
    if (k->task < 0)
        return false;
    return TaskStart(k->task, (void *)USER_ELF_BASE, (void *)USR_SP, 0, 0) == ZUZU_OK;
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
        if (TaskSetPriority(k.task, p) == ZUZU_OK)
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
        TaskSetPriority(a.task, 0);
        TaskSetPriority(b.task, 1);
        SpinnerFree(&a);
        Yield();
        SpinnerFree(&b);
    }
}

int main(void)
{
    UserspaceDebugLog("schedtest: start");
    TestSpinnerDestroy();
    UserspaceDebugLog("schedtest: spinner destroy done");
    TestPriorityOnQueued();
    UserspaceDebugLog("schedtest: priority on queued done");
    TestTwoQueued();

    Check(Sleep(20) == ZUZU_OK, "scheduler still runs after the stress");
    UserspaceDebugLog("schedtest: %s (%d failures)", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
