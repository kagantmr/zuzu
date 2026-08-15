#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zuzu/zuzu.h>

#include "exec.h"
#include "sysd.h"
#include "zuzu/protocols/exec.h"
#include "zuzu/service.h"
#include "zuzu/types.h"
#include <cpio.h>
#include <malloc.h>
#include <stdlib.h>
#include <zuzu/boot.h>
#include <zuzu/channel.h>
#include <zuzu/err.h>
#include <zuzu/fsd_client.h>
#include <zuzu/lmsg.h>
#include <zuzu/syspage.h>
#include <zuzu/version.h>
#include <zuzu/fnv1a.h>

/* sysd's own "sys" service port (SYSD_EXEC only, see nt_handle_msg below) —
 * distinct from nameserver_port, which is nameserver's serving port that
 * sysd created and placed in its own handle slot 0 (see nameserver_setup).
 * Every child sysd spawns thereafter inherits that slot automatically
 * (SysPSpawn copies handle slots 0-3 from parent to child). */
static int32_t port;
static int32_t nameserver_port;
static uint32_t nameserver_pid;
static FsdConn fsd_conn;

/* sysd is spawned via KernelProcessLoad, not the ordinary SysPSpawn chain,
 * so it never inherits anything at slot 0 — it creates nameserver's port
 * itself and must place it there by construction: this must be the very
 * first handle-allocating call sysd ever makes (ZuzuPortCreate lands at the
 * first free slot, which is 0 only if nothing else has allocated yet). */
static int nameserver_setup(const void *initrd, uint32_t initrd_sz)
{
    nameserver_port = ZuzuPortCreate();
    if (nameserver_port < 0)
        return nameserver_port;

    const void *elf_data;
    size_t elf_size;
    if (!cpio_find(initrd, initrd_sz, NAMESERVER_ELF_PATH, &elf_data, &elf_size))
        return ERR_NOENT;

    TSpawnResult ts = ZuzuPSpawn("nameserver");
    if (ts.taskHandle < 0)
        return ts.taskHandle;
    nameserver_pid = (uint32_t)ts.pid;

    /* Pre-kickstart grant, same as any other spawned child — nameserver's
     * table is still empty (FROZEN, hasn't executed a single instruction),
     * so this lands at its own slot 0 too. */
    if (ZuzuGrant(nameserver_port, (Pid)nameserver_pid, 0) < 0)
        return ERR_NOPERM;

    ExecReply reply;
    if (exec_inject((uint32_t)ts.taskHandle, elf_data, elf_size, NULL, 0, 0, &reply) != 0)
        return EXEC_EBADELF;

    ZuzuSetLabel(ts.taskHandle, LABEL_OF("/nt"));

    ZuzuKickstart(ts.taskHandle, reply.entry, reply.sp, reply.argc, reply.argv_va);
    return ZUZU_OK;
}

/* sysd is an ordinary nameserver client for its own "sys" service, exactly
 * like devmgr or any other registrant — no special-casing on nameserver's
 * side. */
static int sysd_register_self(void)
{
    port = ZuzuPortCreate();
    if (port < 0)
        return port;

    return RegisterService("/svc/sysd", port);
}

/* Only SYSD_EXEC arrives on sysd's own port now — NT_REGISTER/NT_LOOKUP go
 * straight to nameserver (NT_PORT), which every process either inherits
 * from sysd (SysPSpawn's slot 0-3 copy) or is granted directly (devmgr). */
static void nt_handle_msg(Message msg)
{
    if (msg.w2 >= sizeof(ExecRequestHeader) && msg.w2 <= LMSG_BUF_SIZE &&
        ((ExecRequestHeader *)LmsgBuf())->cmd == SYSD_EXEC)
    {
        Handle reply_handle = (Handle)msg.w0;
        size_t req_len = msg.w2;
        ExecRequestHeader *hdr = (ExecRequestHeader *)LmsgBuf();

        size_t path_off = sizeof(ExecRequestHeader);
        size_t path_bytes = (size_t)hdr->path_len + 1;
        if (path_bytes == 0 || path_off + path_bytes > req_len ||
            ((char *)LmsgBuf())[path_off + hdr->path_len] != '\0')
        {
            ZuzuMsgReply(reply_handle, (uint32_t)ERR_NOENT, 0, 0);
            return;
        }

        /* The lazy fsd-connect below (LookupServiceWithPid) issues its own
         * Lcall, which reuses this same thread's LmsgBuf() as scratch space
         * -- clobbering the path/argbuf bytes still referenced below if they
         * pointed straight into it. Snapshot the request out of LmsgBuf()
         * first so it survives. */
        static uint8_t reqbuf[LMSG_BUF_SIZE];
        memcpy(reqbuf, LmsgBuf(), req_len);
        hdr = (ExecRequestHeader *)reqbuf;
        const char *path = (const char *)reqbuf + path_off;
        const char *argbuf = (const char *)reqbuf + path_off + path_bytes;
        size_t argbuf_len = req_len - path_off - path_bytes;

        /* --- lazy-connect to fsd (once) --- */
        if (!fsd_conn.ready)
        {
            Handle fsd_h = 0;
            Pid fsd_p = 0;
            if ((fsd_h = LookupServiceWithPid("/svc/fsd", &fsd_p)) < 0)
            {
                ZuzuMsgReply(reply_handle, (uint32_t)ERR_NOENT, 0, 0);
                return;
            }
            if (FsdAttach(&fsd_conn, (int32_t)fsd_h, fsd_p, FSD_SHM_DEFAULT) != ZUZU_OK)
            {
                ZuzuMsgReply(reply_handle, (uint32_t)EXEC_EIO, 0, 0);
                return;
            }
        }

        size_t plen = strlen(path);
        if (plen == 0 || plen >= 4096)
        {
            ZuzuMsgReply(reply_handle, (uint32_t)ERR_NOENT, 0, 0);
            return;
        }

        FsdStat st;
        memset(&st, 0, sizeof(st));
        if (FsdGetStat(&fsd_conn, path, &st) != ZUZU_OK)
        {
            ZuzuMsgReply(reply_handle, (uint32_t)ERR_NOENT, 0, 0);
            return;
        }

        uint32_t file_size = st.size;
        if (file_size == 0 || st.type == FSD_TYPE_DIR)
        {
            ZuzuMsgReply(reply_handle, (uint32_t)EXEC_EBADELF, 0, 0);
            return;
        }

        uint32_t fd = 0;
        if (FsdOpen(&fsd_conn, path, FSD_MODE_READ, &fd) != ZUZU_OK)
        {
            ZuzuMsgReply(reply_handle, (uint32_t)EXEC_EIO, 0, 0);
            return;
        }

        uint8_t *elf = (uint8_t *)malloc(file_size);
        if (!elf)
        {
            FsdClose(&fsd_conn, fd);
            ZuzuMsgReply(reply_handle, (uint32_t)ERR_NOMEM, 0, 0);
            return;
        }

        uint32_t total = 0;
        while (total < file_size)
        {
            uint32_t got = 0;
            if (FsdRead(&fsd_conn, fd, elf + total, file_size - total, &got) != ZUZU_OK || got == 0)
                break;
            total += got;
        }
        FsdClose(&fsd_conn, fd);

        if (total != file_size)
        {
            free(elf);
            ZuzuMsgReply(reply_handle, (uint32_t)EXEC_EIO, 0, 0);
            return;
        }

        ExecReply reply;
        int rc = exec_inject((uint32_t)hdr->taskHandle, elf, file_size, argbuf_len ? argbuf : NULL,
                             argbuf_len, hdr->argc, &reply);
        free(elf);
        if (rc != 0)
        {
            ZuzuMsgReply(reply_handle, (uint32_t)EXEC_EBADELF, 0, 0);
            return;
        }

        memcpy(LmsgBuf(), &reply, sizeof(reply));
        (void)ChannelReply((Handle)reply_handle, LmsgBuf(), sizeof(reply));
        return;
    }
}

#define MAX_BOOT_ENTRIES 16

typedef struct
{
    char path[64];
    char name[32];
    const void *elf_data; /* into CPIO mapping; NULL if SD-only */
    size_t elf_size;
    int32_t taskHandle;
    uint32_t pid;
    ExecReply reply;
    bool in_cpio;
    bool injected;
    bool spawn_last;
    char svc_path[NT_MAX_PATH];
} boot_entry_t;

static boot_entry_t boot_entries[MAX_BOOT_ENTRIES];
static int boot_count;

static char deferred_paths[MAX_BOOT_ENTRIES][64];
static int deferred_count;

#define WAIT_TIMEOUT_MS 30000u
#define WAIT_SLICE_MS 10u

static bool recvany_to_ipcmsg(const WaitanyResult *res, Message *msg)
{
    if (!res || !msg)
        return false;

    if (res->kind == WAITANY_KIND_SEND || res->kind == WAITANY_KIND_CALL)
    {
        msg->w0 = res->source;
        msg->w1 = res->w1;
        msg->w2 = res->w2;
        msg->w3 = res->w3;
        return true;
    }

    /* Treat IRQ/notification wakes as a simple event: propagate source and
     * the notification bitmask in w1. Consumers can interpret `matched_index`
     * if needed via the recvany_result metadata (not present in Message).
     */
    if (res->kind == WAITANY_KIND_NTFN)
    {
        msg->w0 = (int32_t)res->source;
        msg->w1 = res->w1; /* notification bits */
        msg->w2 = res->w2;
        msg->w3 = res->w3;
        return true;
    }

    return false;
}

static boot_entry_t *find_boot_entry_by_pid(uint32_t pid)
{
    for (int i = 0; i < boot_count; i++)
    {
        if (boot_entries[i].injected && boot_entries[i].pid == pid)
            return &boot_entries[i];
    }
    return NULL;
}

/* Crash-looking deaths (faults, OOM) get respawned; a clean exit or an
 * explicit pkill from another process (KILL_BY_PARENT) means someone
 * wanted this process gone, so leave it dead. */
static bool should_respawn(int32_t status)
{
    if (!WAS_KILLED(status))
        return false;

    switch (KILL_REASON(status))
    {
    case KILL_FAULT_DATA:
    case KILL_FAULT_PREFETCH:
    case KILL_FAULT_UNDEF:
    case KILL_FAULT_ALIGN:
    case KILL_OOM:
        return true;
    default:
        return false;
    }
}

static void respawn_entry(boot_entry_t *e)
{
    TSpawnResult ts = ZuzuPSpawn(e->name);
    if (ts.taskHandle < 0)
        return;

    e->taskHandle = ts.taskHandle;
    e->pid = ts.pid;
    e->injected = false;

    ZuzuSetLabel(ts.taskHandle, LABEL_OF(e->svc_path[0] ? e->svc_path : e->path));

    if (exec_inject((uint32_t)ts.taskHandle, e->elf_data, e->elf_size, NULL, 0, 0, &e->reply) != 0)
        return;

    e->injected = true;
    ZuzuKickstart(e->taskHandle, e->reply.entry, e->reply.sp, e->reply.argc, e->reply.argv_va);
}

/* Drains every zombie currently on our child list. Supervised children
 * (tracked in boot_entries) are checked against the respawn policy;
 * everything else is an orphan reparented to us (sysd is pid 1) and is
 * just reaped and discarded. */
static void reap_all(void)
{
    Err status;
    Pid pid;

    while ((pid = ZuzuWait(-1, &status, WNOHANG)) > 0)
    {
        boot_entry_t *e = find_boot_entry_by_pid((uint32_t)pid);
        ScrubServicePid(pid);

        if (e && should_respawn(status))
            respawn_entry(e);
    }
}

static bool WaitForService(const char *name)
{
    Handle handle = 0;
    Pid pid = 0;
    Duration waited_ms = 0;
    Handle recv_handles[1] = {(Handle)port};

    while ((handle = LookupServiceWithPid(name, &pid)) < 0 && waited_ms < WAIT_TIMEOUT_MS)
    {
        reap_all();

        WaitanyResult any = {0};
        if (ZuzuWaitany(recv_handles, 1, WAIT_SLICE_MS, &any) == 0)
        {
            Message msg;
            if (recvany_to_ipcmsg(&any, &msg))
                nt_handle_msg(msg);
        }
        waited_ms += WAIT_SLICE_MS;
    }

    return ((handle = LookupServiceWithPid(name, &pid)) >= 0);
}

/* A process that dies with zombie children reparents them to us (see
 * process_kill, kernel/proc/process.c), relying on reap_all() to actually
 * free them. A blocking TIMEOUT_INFINITE recv only re-runs reap_all() when
 * new traffic happens to arrive, so an orphan reparented to us during a
 * quiet stretch would sit un-reaped (leaking its kstack + L1 table)
 * indefinitely. Poll instead, bounding that window. */
#define REAP_POLL_MS 1000u

void sysd_loop(void)
{
    while (1)
    {
        reap_all();
        nt_handle_msg(ZuzuMsgRecv(port, REAP_POLL_MS));
    }
}

/* ================================================================
 *  Boot sequence
 * ================================================================ */

static const char *basename(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            b = p + 1;
    return b;
}

static bool role_is_kernel(const char *r, size_t len)
{
    return (len == 4 && memcmp(r, "init", 4) == 0) || (len == 3 && memcmp(r, "dev", 3) == 0) ||
           (len == 6 && memcmp(r, "devmgr", 6) == 0);
}

static void parse_manifest(const char *data, size_t size, const void *cpio, size_t cpio_size)
{
    const char *p = data;
    const char *end = data + size;

    boot_count = 0;
    deferred_count = 0;

    while (p < end && boot_count < MAX_BOOT_ENTRIES)
    {
        const char *eol = p;
        while (eol < end && *eol != '\n')
            eol++;

        size_t ll = (size_t)(eol - p);
        if (ll == 0 || p[0] == '#')
        {
            p = eol + 1;
            continue;
        }

        while (ll > 0 && (p[ll - 1] == '\r' || p[ll - 1] == ' ' || p[ll - 1] == '\t'))
            ll--;

        /* find pipe */
        size_t pipe = 0;
        while (pipe < ll && p[pipe] != '|')
            pipe++;
        if (pipe == 0 || pipe >= ll)
        {
            p = eol + 1;
            continue;
        }

        const char *ps = p;
        size_t pl = pipe;
        const char *rs = p + pipe + 1;
        size_t rl = ll - pipe - 1;

        while (pl > 0 && (ps[pl - 1] == ' ' || ps[pl - 1] == '\t'))
            pl--;
        while (rl > 0 && (*rs == ' ' || *rs == '\t'))
        {
            rs++;
            rl--;
        }
        while (rl > 0 && (rs[rl - 1] == ' ' || rs[rl - 1] == '\t'))
            rl--;

        size_t pipe2 = 0;
        while (pipe2 < rl && rs[pipe2] != '|')
            pipe2++;

        const char *ss = NULL;
        size_t sl = 0;
        if (pipe2 < rl)
        {
            ss = rs + pipe2 + 1;
            sl = rl - pipe2 - 1;
            rl = pipe2;
            while (rl > 0 && (rs[rl - 1] == ' ' || rs[rl - 1] == '\t'))
                rl--;
            while (sl > 0 && (*ss == ' ' || *ss == '\t'))
            {
                ss++;
                sl--;
            }
            while (sl > 0 && (ss[sl - 1] == ' ' || ss[sl - 1] == '\t'))
                sl--;
        }

        if (role_is_kernel(rs, rl))
        {
            p = eol + 1;
            continue;
        }
        if (pl >= 64)
        {
            p = eol + 1;
            continue;
        }

        /* normalise path: bare name -> bin/<name> */
        char full[64];
        if (memchr(ps, '/', pl))
        {
            memcpy(full, ps, pl);
            full[pl] = '\0';
        }
        else
        {
            memcpy(full, "bin/", 4);
            memcpy(full + 4, ps, pl);
            full[4 + pl] = '\0';
        }

        const void *elf = NULL;
        size_t esz = 0;

        if (cpio_find(cpio, cpio_size, full, &elf, &esz))
        {
            boot_entry_t *e = &boot_entries[boot_count++];
            strcpy(e->path, full);
            const char *bn = basename(full);
            strncpy(e->name, bn, sizeof(e->name) - 1);
            e->name[sizeof(e->name) - 1] = '\0';
            if (ss && sl > 0 && sl < sizeof(e->svc_path))
            {
                memcpy(e->svc_path, ss, sl);
                e->svc_path[sl] = '\0';
            }
            else
            {
                e->svc_path[0] = '\0';
            }
            e->elf_data = elf;
            e->elf_size = esz;
            e->taskHandle = -1;
            e->pid = 0;
            e->in_cpio = true;
            e->injected = false;
            e->spawn_last = false;

            /* role field may include flags after a ':' e.g. "tty:late" */
            const char *colon = memchr(rs, ':', rl);
            if (colon)
            {
                size_t role_len = (size_t)(colon - rs);
                const char *flags = colon + 1;
                size_t flags_len = rl - role_len - 1;
                if (flags_len > 0)
                {
                    if (flags_len == 4 && memcmp(flags, "late", 4) == 0)
                        e->spawn_last = true;
                    else if (flags_len == 4 && memcmp(flags, "last", 4) == 0)
                        e->spawn_last = true;
                    else if (flags_len == 10 && memcmp(flags, "spawn_last", 10) == 0)
                        e->spawn_last = true;
                }
            }
        }
        else if (deferred_count < MAX_BOOT_ENTRIES)
        {
            strcpy(deferred_paths[deferred_count++], full);
        }

        p = eol + 1;
    }
}

int main(int argc, char **argv)
{
    const Syspage *sp = (const Syspage *)SYSPAGE_VA;

    /**
     * before ANYTHING happens, check if the kernel version is compatible with this sysd
     *
     * sysd will pull ZUZUOS_MIN_KERNEL_MAJOR from <zuzu/version.h> and compare it to its own.
     * Later on, we can embed sysd its own version, but for now since the userspace is scaling
     * altogether instead of seperately versioned binaries, we can check kernel version and assume
     * it will not work.
     *
     * Later on, sysd could check against old binaries and warn the user that their programs are
     * outdated.
     *
     */
    if (((sp->kernel_ver & 0x00FF0000) >> 16) < ZUZUOS_MIN_KERNEL_MAJOR)
    {
        ZuzuPQuit(FATAL_TAG | FATAL_KERNEL_OUTDATED);
    }

    /* argv[1]/argv[2] = initrd VA + size; argv[3]/argv[4] = devmgr's entry
     * point/stack pointer, peeked from its ELF header by the kernel before
     * devmgr existed (see kernel/kmain.c's two-pass devmgr boot) — all set
     * by the kernel in boot_program() (kernel/kmain.c). Passed explicitly
     * rather than assumed at a fixed address/value: the initrd's physical
     * source isn't necessarily page-aligned, and devmgr's entry/sp depend
     * on its actual ELF contents. */
    if (argc < 5)
        return 1;
    const void *initrd = (const void *)strtoul(argv[1], NULL, 16);
    uint32_t initrd_sz = (uint32_t)strtoul(argv[2], NULL, 10);
    VirtAddr devmgr_entry = (VirtAddr)strtoul(argv[3], NULL, 16);
    VirtAddr devmgr_sp = (VirtAddr)strtoul(argv[4], NULL, 16);

    ZuzuSetLabel(-2 /* LABEL_SELF sentinel */, LABEL_OF("/svc/sysd"));

    /* ---- nameserver: spawn, grant it its own port, place that port in
     * our own slot 0 ---- */

    if (nameserver_setup(initrd, initrd_sz) != 0)
        return 1;

    /* ---- sysd's own "sys" service ---- */

    if (sysd_register_self() != 0)
        return 1;

    /* ---- grant devmgr the nameserver port, then kickstart it ---- */

    {
        int32_t devmgr_ns_slot = ZuzuGrant(nameserver_port, DEVMGR_PID, 0);
        if (devmgr_ns_slot >= 0)
        {
            ZuzuSetLabel(SYSD_DEVMGR_TASK_HANDLE_SLOT, LABEL_OF("/svc/devmgr"));
            ZuzuKickstart(SYSD_DEVMGR_TASK_HANDLE_SLOT, devmgr_entry, devmgr_sp,
                          (uint32_t)devmgr_ns_slot, nameserver_pid);
        }
    }

    /* ---- read boot manifest from CPIO ---- */

    const void *mdata;
    size_t msize;
    if (!cpio_find(initrd, initrd_sz, "boot.manifest", &mdata, &msize))
        return 1;

    parse_manifest((const char *)mdata, msize, initrd, initrd_sz);

    /* ---- pspawn + inject every CPIO-resident program ---- */

    for (int i = 0; i < boot_count; i++)
    {
        boot_entry_t *e = &boot_entries[i];
        if (!e->in_cpio || e->spawn_last)
            continue;

        TSpawnResult ts = ZuzuPSpawn(e->name);
        if (ts.taskHandle < 0)
            continue;

        e->taskHandle = ts.taskHandle;
        e->pid = ts.pid;

        ZuzuSetLabel(ts.taskHandle, LABEL_OF(e->svc_path[0] ? e->svc_path : e->path));

        if (exec_inject((uint32_t)ts.taskHandle, e->elf_data, e->elf_size, NULL, 0, 0, &e->reply) !=
            0)
            continue;
        e->injected = true;
    }

    /* ---- kickstart ---- */

    for (int i = 0; i < boot_count; i++)
    {
        boot_entry_t *e = &boot_entries[i];
        if (!e->injected)
            continue;

        ZuzuKickstart(e->taskHandle, e->reply.entry, e->reply.sp, e->reply.argc, e->reply.argv_va);
    }

    /* devmgr was explicitly kickstarted above; wait for it to register so
     * other services can find it once they start. */
    WaitForService("/svc/devmgr");

    // wait for fsd so we can spawn deferred entries through it once it's ready,
    // but only if fsd is actually in this boot manifest — otherwise this is a
    // guaranteed 30s stall waiting for a service that will never register
    // (e.g. a trimmed boot manifest without fsd/pl181drv).
    bool have_fsd = false;
    for (int i = 0; i < boot_count; i++)
    {
        if (boot_entries[i].in_cpio && strcmp(boot_entries[i].name, "fsd") == 0)
        {
            have_fsd = true;
            break;
        }
    }
    if (have_fsd)
        WaitForService("/svc/fsd");

    /* Spawn any entries marked spawn_last after services are available. */
    for (int i = 0; i < boot_count; i++)
    {
        boot_entry_t *e = &boot_entries[i];
        if (!e->in_cpio || !e->spawn_last)
            continue;

        TSpawnResult ts = ZuzuPSpawn(e->name);
        if (ts.taskHandle < 0)
            continue;

        e->taskHandle = ts.taskHandle;
        e->pid = ts.pid;

        ZuzuSetLabel(ts.taskHandle, LABEL_OF(e->svc_path[0] ? e->svc_path : e->path));

        if (exec_inject((uint32_t)ts.taskHandle, e->elf_data, e->elf_size, NULL, 0, 0, &e->reply) !=
            0)
            continue;
        e->injected = true;

        ZuzuKickstart(e->taskHandle, e->reply.entry, e->reply.sp, e->reply.argc, e->reply.argv_va);
    }

    sysd_loop();
    return 0;
}
