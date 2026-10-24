#include <ztask/ztask.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>

static void ZTaskTrampoline(ZTask *t)
{
    t->entry(t->arg);
    if (t->detached)
        ArchUnmapAndQuit(t->base);
    Quit(ZUZU_OK);
}

static ZTask *Spawn(void (*entry)(void *), void *arg, size_t stack_size, int detached)
{
    if (stack_size == 0)
        stack_size = ZTASK_DEFAULT_STACK;
    if (stack_size < (2 * sizeof(ZTask)) + 256)
        return (ZTask *)(intptr_t)ERR_BADARG;

    char *base = MemMapAnon(stack_size, 0, PROT_READ | PROT_WRITE);
    if (PtrIsErr(base))
        return (ZTask *)base;

    VirtAddr top = ((VirtAddr)base + stack_size - sizeof(ZTask)) & ~(VirtAddr)7;
    ZTask *t = (ZTask *)top;
    t->base = base;
    t->size = stack_size;
    t->entry = entry;
    t->arg = arg;
    t->detached = detached;

    t->handle = CreateTask(-1);
    if (t->handle < 0) {
        Err rc = t->handle;
        MemUnmap(base);
        return (ZTask *)(intptr_t)rc;
    }

    Err rc = TaskStart(t->handle, ZTaskTrampoline, t, (uint32_t)(VirtAddr)t, 0);
    if (rc != ZUZU_OK) {
        HandleClose(t->handle);
        MemUnmap(base);
        return (ZTask *)(intptr_t)rc;
    }
    return t;
}

ZTask *ZTaskSpawn(void (*entry)(void *), void *arg, size_t stack_size)
{
    return Spawn(entry, arg, stack_size, 0);
}

Err ZTaskSpawnDetached(void (*entry)(void *), void *arg, size_t stack_size)
{
    Handle h;
    ZTask *t = Spawn(entry, arg, stack_size, 1);
    if (PtrIsErr(t))
        return (Err)(intptr_t)t;
    h = t->handle;
    HandleClose(h);
    return ZUZU_OK;
}

Err ZTaskJoin(ZTask *t, Duration timeout, int32_t *exit_status)
{
    TaskWaitResult r = WaitOnTask(t->handle, timeout);
    if (r.status != ZUZU_OK)
        return r.status;

    if (exit_status)
        *exit_status = r.value;
    HandleClose(t->handle);
    MemUnmap(t->base);
    return r.outcome == TASK_EXITED ? ZUZU_OK : ERR_DEAD;
}
