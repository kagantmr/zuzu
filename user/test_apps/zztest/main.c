/* zztest - Loaf syscall ABI regression suite.
 *
 * Tests every frozen syscall's observable contract through the zuzu_*
 * wrappers: success paths AND the documented error cases from
 * syscall_nums.h. Supersedes the old cycletest; the thread/lmsg round-trip
 * coverage from test/main.c is folded into the IPC section.
 *
 * Sections: mem, ipc, handles, tasks, vfp, version, security, leaks.
 * Output: ok/FAIL per assertion, per-section summary table.
 * Exit status = total failure count.
 *
 * Cross-process tests spawn /bin/zztest_child through sysd exec (the same
 * pspawn -> grant -> SYSD_EXEC -> kickstart -> wait path zzsh uses).
 *
 * Leak guards read the free-page count exactly the way zzsh `free` does
 * (syspage mem_free_kb / 4) with ZERO tolerance. Each loop is preceded by
 * one warm-up iteration so lazy one-time allocations (kernel heap/slab/L2
 * pool growth, malloc arena, stdio tty lookup) land before the baseline,
 * and followed by a short sleep so the deferred thread reaper quiesces.
 * Neither is slop: the post-baseline delta must be exactly zero.
 *
 * Known coverage gaps (flagged, not silently skipped):
 *  - memprotect/memmap EXEC-on-device rejection: an unprivileged process
 *    cannot obtain a device capability (devmgr injects them into drivers
 *    only), so the device-region paths are untestable from here.
 */
#include <zuzu/zuzu.h>
#include <zuzu/lmsg.h>
#include <zuzu/channel.h>
#include <zuzu/memprot.h>
#include <zuzu/tcb.h>
#include <zuzu/syspage.h>
#include <zuzu/protocols/nt_protocol.h>
#include <zuzu/protocols/sysd_protocol.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mem.h>

#define STACK_SIZE      4096u
#define LEAK_ITERS      50
#define CHILD_PATH      "/bin/zztest_child"
#define CHILD_NAME      "zztest_child"
#define VM_PROT_USER_BIT (1u << 3)   /* kernel-internal bit, must be rejected */

/* ---------------- harness ---------------- */

#define MAX_SECTIONS 10
static struct { const char *name; int pass, fail; } sections[MAX_SECTIONS];
static int cur_sec = -1;

static void section(const char *name)
{
    cur_sec++;
    sections[cur_sec].name = name;
    sections[cur_sec].pass = 0;
    sections[cur_sec].fail = 0;
    printf("\n--- %s ---\n", name);
}

#define CHECK(c, m) do {                                              \
    if (c) { sections[cur_sec].pass++; printf("ok:   %s\n", m); }     \
    else   { sections[cur_sec].fail++; printf("FAIL: %s\n", m); }     \
} while (0)

/* CHECK with the observed value printed on failure (error-code asserts) */
#define CHECK_EQ(got, want, m) do {                                   \
    int32_t g_ = (int32_t)(got);                                       \
    if (g_ == (int32_t)(want)) {                                       \
        sections[cur_sec].pass++; printf("ok:   %s\n", m);             \
    } else {                                                           \
        sections[cur_sec].fail++;                                      \
        printf("FAIL: %s (got %d, want %d)\n", m, g_, (int32_t)(want));\
    }                                                                  \
} while (0)

static uint32_t pages_free(void)
{
    const syspage_t *sp = (const syspage_t *)SYSPAGE;
    return sp->mem_free_kb / 4;   /* exactly what zzsh `free` prints */
}

static uint32_t uptime_ms(void)
{
    const syspage_t *sp = (const syspage_t *)SYSPAGE;
    return (uint32_t)((sp->uptime_ticks * 1000u) / sp->tick_hz);
}

static int32_t mm_err(void *p) { return (int32_t)(uintptr_t)p; }

/* raw waitany svc: the zuzu_waitany wrapper owns result->size, so the
 * struct-versioning test must issue the svc itself. */
static int32_t raw_waitany(const handle_t *handles, uint32_t count,
                           uint32_t timeout_ms, waitany_result_t *result)
{
    register uintptr_t r0 __asm__("r0") = (uintptr_t)handles;
    register uint32_t  r1 __asm__("r1") = count;
    register uint32_t  r2 __asm__("r2") = timeout_ms;
    register uintptr_t r3 __asm__("r3") = (uintptr_t)result;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), [num] "i"(SYS_WAITANY)
        : "memory");
    return (int32_t)r0;
}

/* ---------------- child spawn plumbing (mirrors zzsh cmd_exec) -------- */

static int32_t g_sysd_port = -1;
static zpid_t  g_sysd_pid;

static int sysd_setup(void)
{
    msg_t r = zuzu_msg_call(NT_PORT, NT_LOOKUP, nt_pack(NT_NAME_SYS), 0);
    if ((int32_t)r.r1 != NT_LU_OK)
        return -1;
    g_sysd_port = (int32_t)r.r2;
    g_sysd_pid  = (zpid_t)r.r3;
    return 0;
}

typedef struct { handle_t task; zpid_t pid; } child_t;

/* Spawn CHILD_PATH with argv = {CHILD_NAME, arg1[, arg2]}.
 * grant_h >= 0: granted into the child pre-kickstart; the child-side slot
 * number is passed as argv[2] (arg2 must then be NULL).
 * Returns 0, or a negative err. */
static int32_t child_spawn(const char *arg1, handle_t grant_h, child_t *out)
{
    tspawn_result_t ts = zuzu_pspawn(CHILD_NAME);
    if (ts.task_handle < 0)
        return ts.task_handle;

    char arg2[16];
    int argc = 2;
    if (grant_h >= 0) {
        int32_t child_slot = zuzu_grant(grant_h, ts.pid);
        if (child_slot < 0) {
            zuzu_pkill(ts.task_handle);
            return child_slot;
        }
        snprintf(arg2, sizeof(arg2), "%d", (int)child_slot);
        argc = 3;
    }

    int32_t sysd_task = zuzu_grant(ts.task_handle, g_sysd_pid);
    if (sysd_task < 0) {
        zuzu_pkill(ts.task_handle);
        return sysd_task;
    }

    /* request = header + "path\0" + "argv0\0argv1\0[argv2\0]" */
    char argbuf[96];
    size_t argpos = 0;
    memcpy(argbuf + argpos, CHILD_NAME, strlen(CHILD_NAME) + 1);
    argpos += strlen(CHILD_NAME) + 1;
    memcpy(argbuf + argpos, arg1, strlen(arg1) + 1);
    argpos += strlen(arg1) + 1;
    if (argc == 3) {
        memcpy(argbuf + argpos, arg2, strlen(arg2) + 1);
        argpos += strlen(arg2) + 1;
    }

    size_t path_len = strlen(CHILD_PATH);
    uint8_t req[sizeof(exec_request_hdr_t) + sizeof(CHILD_PATH) + sizeof(argbuf)];
    exec_request_hdr_t *hdr = (exec_request_hdr_t *)req;
    hdr->cmd = SYSD_EXEC;
    hdr->_pad = 0;
    hdr->task_handle = (uint16_t)sysd_task;
    hdr->path_len = (uint16_t)path_len;
    hdr->argc = (uint16_t)argc;
    hdr->pid = ts.pid;
    memcpy(req + sizeof(*hdr), CHILD_PATH, path_len + 1);
    memcpy(req + sizeof(*hdr) + path_len + 1, argbuf, argpos);

    exec_reply_t reply;
    int32_t rc = chan_call((handle_t)g_sysd_port, req,
                           (uint32_t)(sizeof(*hdr) + path_len + 1 + argpos),
                           &reply, sizeof(reply));
    if (rc < 0) {
        zuzu_pkill(ts.task_handle);
        return rc;
    }
    if (rc != (int32_t)sizeof(exec_reply_t)) {
        zuzu_pkill(ts.task_handle);
        return ERR_MALFORMED;
    }

    rc = zuzu_kickstart(ts.task_handle, reply.entry, reply.sp,
                        (uint32_t)argc, reply.argv_va);
    if (rc != 0) {
        zuzu_pkill(ts.task_handle);
        return rc;
    }

    out->task = ts.task_handle;   /* consumed by kickstart (slot freed) */
    out->pid = ts.pid;
    return 0;
}

/* spawn + wait; returns wait()'s rc, exit status in *status */
static int32_t child_run(const char *arg1, handle_t grant_h, int32_t *status)
{
    child_t c;
    int32_t rc = child_spawn(arg1, grant_h, &c);
    if (rc < 0)
        return rc;
    rc = zuzu_wait(c.pid, status, 0);
    return (rc == c.pid) ? 0 : (rc < 0 ? rc : ERR_MALFORMED);
}

/* ---------------- IPC worker threads ---------------- */

static handle_t g_port = -1;
#define REQ  "zztest lmsg request payload"
#define RESP "zztest lmsg reply payload"

static volatile int g_worker_ok;

static void lcall_worker(void *arg)
{
    (void)arg;
    char buf[LMSG_BUF_SIZE];
    lmsg_write(REQ, sizeof(REQ));
    msg_t r = zuzu_msg_lcall(g_port, sizeof(REQ));
    if ((int32_t)r.r0 == 0) {
        lmsg_read(buf, r.r1);
        g_worker_ok = (r.r1 == sizeof(RESP) &&
                       memcmp(buf, RESP, sizeof(RESP)) == 0);
    }
    zuzu_tquit(0);
}

static void call_worker(void *arg)
{
    (void)arg;
    msg_t r = zuzu_msg_call(g_port, 0x11, 0x22, 0x33);
    g_worker_ok = ((int32_t)r.r0 == 0 &&
                   r.r1 == 0xA1 && r.r2 == 0xA2 && r.r3 == 0xA3);
    zuzu_tquit(0);
}

static void send_worker(void *arg)
{
    (void)arg;
    zuzu_sleep(20);   /* let main block in waitany first */
    g_worker_ok = (zuzu_msg_send(g_port, 0x51, 0x52, 0x53) == 0);
    zuzu_tquit(0);
}

static void recv_dead_worker(void *arg)
{
    (void)arg;
    msg_t r = zuzu_msg_recv(g_port, TIMEOUT_INFINITE);
    g_worker_ok = ((int32_t)r.r0 == ERR_DEAD);
    zuzu_tquit(0);
}

static void trivial_worker(void *arg)
{
    (void)arg;
    zuzu_tquit(0);
}

static volatile int g_spin_quit[TCB_MAX_SLOTS];

static void spin_worker(void *arg)
{
    int idx = (int)(uintptr_t)arg;
    while (!g_spin_quit[idx])
        zuzu_sleep(2);
    zuzu_tquit(0x40 + idx);
}

/* memmap a worker stack or die */
static void *stack_alloc(void)
{
    void *s = zuzu_memmap(HANDLE_ANON, STACK_SIZE, VM_PROT_RW, 0);
    return zuzu_is_err(s) ? NULL : s;
}

/* ---------------- sections ---------------- */

static void sec_mem(void)
{
    section("mem");

    /* anon success path: alloc / write / readback / unmap */
    uint8_t *a = (uint8_t *)zuzu_memmap(HANDLE_ANON, 8192, VM_PROT_RW, 0);
    CHECK(!zuzu_is_err(a), "anon memmap 8KB");
    for (uint32_t i = 0; i < 8192; i++) a[i] = (uint8_t)(i * 7);
    int ok = 1;
    for (uint32_t i = 0; i < 8192; i++) if (a[i] != (uint8_t)(i * 7)) { ok = 0; break; }
    CHECK(ok, "anon write/readback intact");
    CHECK_EQ(zuzu_memunmap(a), 0, "anon memunmap");

    /* anon documented rejections */
    CHECK_EQ(mm_err(zuzu_memmap(HANDLE_ANON, 0, VM_PROT_RW, 0)), ERR_BADARG,
             "anon size=0 -> ERR_BADARG");
    CHECK_EQ(mm_err(zuzu_memmap(HANDLE_ANON, 4095, VM_PROT_RW, 0)), ERR_BADARG,
             "anon size not page-multiple -> ERR_BADARG");
    CHECK_EQ(mm_err(zuzu_memmap(HANDLE_ANON, 33u * 1024 * 1024, VM_PROT_RW, 0)), ERR_OVERFLOW,
             "anon >32MB -> ERR_OVERFLOW");
    CHECK_EQ(mm_err(zuzu_memmap(HANDLE_ANON, 4096, VM_PROT_WRITE | VM_PROT_EXEC, 0)), ERR_BADARG,
             "W+X prot -> ERR_BADARG");
    CHECK_EQ(mm_err(zuzu_memmap(HANDLE_ANON, 4096, VM_PROT_USER_BIT, 0)), ERR_BADARG,
             "VM_PROT_USER bit -> ERR_BADARG");
    CHECK_EQ(mm_err(zuzu_memmap(HANDLE_ANON, 4096, VM_PROT_RW, 1)), ERR_BADARG,
             "flags!=0 -> ERR_BADARG");
    CHECK_EQ(mm_err(zuzu_memmap(9999, 0, VM_PROT_RW, 0)), ERR_BADHANDLE,
             "memmap bogus handle -> ERR_BADHANDLE");

    /* memmap on a non-mappable handle type */
    handle_t p = zuzu_port_create();
    CHECK(p >= 0, "port_create (for BADTYPE probe)");
    CHECK_EQ(mm_err(zuzu_memmap(p, 0, VM_PROT_RW, 0)), ERR_BADTYPE,
             "memmap on port handle -> ERR_BADTYPE");
    CHECK_EQ(zuzu_destroy(p), 0, "destroy probe port");

    /* shm lifecycle (same-process part; cross-proc in handles section) */
    handle_t sh = zuzu_shm_create(4096);
    CHECK(sh >= 0, "shm_create 4KB");
    CHECK_EQ(mm_err(zuzu_memmap(sh, 4096, VM_PROT_RW, 0)), ERR_BADARG,
             "shm memmap with size!=0 -> ERR_BADARG");
    uint8_t *m1 = (uint8_t *)zuzu_memmap(sh, 0, VM_PROT_RW, 0);
    CHECK(!zuzu_is_err(m1), "shm memmap");
    m1[0] = 0x77; m1[4095] = 0x88;
    CHECK_EQ(zuzu_destroy(sh), ERR_BUSY, "destroy-while-mapped -> ERR_BUSY");
    CHECK_EQ(zuzu_memunmap(m1), 0, "shm memunmap");
    uint8_t *m2 = (uint8_t *)zuzu_memmap(sh, 0, VM_PROT_RW, 0);
    CHECK(!zuzu_is_err(m2), "shm REMAP after unmap works");
    CHECK(m2[0] == 0x77 && m2[4095] == 0x88, "shm contents persist across remap");
    CHECK_EQ(zuzu_memunmap(m2), 0, "shm memunmap (2nd)");
    CHECK_EQ(zuzu_destroy(sh), 0, "shm destroy after unmap");

    /* memunmap rejections */
    CHECK_EQ(zuzu_memunmap((void *)0x30000000), ERR_BADARG,
             "memunmap of non-region addr -> ERR_BADARG");
    uint8_t *b = (uint8_t *)zuzu_memmap(HANDLE_ANON, 8192, VM_PROT_RW, 0);
    CHECK(!zuzu_is_err(b), "anon memmap (base-match probe)");
    CHECK_EQ(zuzu_memunmap(b + 4096), ERR_BADARG,
             "memunmap of non-base addr inside region -> ERR_BADARG");
    CHECK_EQ(zuzu_memunmap(b), 0, "probe region unmapped at base");
    CHECK_EQ(zuzu_memunmap((void *)SYSPAGE_VA), ERR_NOPERM,
             "syspage (pinned) unmap -> ERR_NOPERM");
    CHECK_EQ(zuzu_memunmap((void *)((uintptr_t)zuzu_tcb() & ~0xFFFu)), ERR_NOPERM,
             "TCB page (pinned) unmap -> ERR_NOPERM");

    /* memprotect */
    uint8_t *c = (uint8_t *)zuzu_memmap(HANDLE_ANON, 4096, VM_PROT_RW, 0);
    CHECK(!zuzu_is_err(c), "anon memmap (memprotect probe)");
    c[0] = 1; /* fault the page in while writable */
    CHECK_EQ(zuzu_memprotect(c, 4096, VM_PROT_READ), 0, "memprotect RW -> R");
    CHECK_EQ(zuzu_memprotect(c, 4096, VM_PROT_RW), 0, "memprotect R -> RW");
    c[0] = 2;
    CHECK(c[0] == 2, "write works after re-protect");
    CHECK_EQ(zuzu_memprotect(c, 4096, VM_PROT_WRITE | VM_PROT_EXEC), ERR_BADARG,
             "memprotect W+X -> ERR_BADARG");
    CHECK_EQ(zuzu_memprotect(c, 4096, VM_PROT_USER_BIT), ERR_NOPERM,
             "memprotect with USER bit -> ERR_NOPERM");
    CHECK_EQ(zuzu_memprotect((void *)SYSPAGE_VA, 4096, VM_PROT_RW), ERR_BADARG,
             "memprotect pinned syspage -> rejected");
    CHECK_EQ(zuzu_memprotect((void *)0x30000000, 4096, VM_PROT_READ), ERR_BADARG,
             "memprotect non-region -> ERR_BADARG");
    CHECK_EQ(zuzu_memunmap(c), 0, "memprotect probe unmapped");
    /* EXEC-on-device rejection: untestable without a device cap (see header) */
}

static void sec_ipc(void)
{
    section("ipc");

    g_port = zuzu_port_create();
    CHECK(g_port >= 0, "port_create");

    /* recv poll on empty port */
    msg_t e = zuzu_msg_recv(g_port, TIMEOUT_POLL);
    CHECK_EQ(e.r0, ERR_TIMEOUT, "recv poll empty -> ERR_TIMEOUT");

    /* send/recv round trip (worker sends after we block) */
    void *st = stack_alloc();
    CHECK(st != NULL, "worker stack");
    g_worker_ok = 0;
    tid_t t = zuzu_tmake(send_worker, (char *)st + STACK_SIZE, NULL);
    CHECK(t > 0, "tmake send_worker");
    msg_t m = zuzu_msg_recv(g_port, TIMEOUT_INFINITE);
    CHECK((int32_t)m.r0 > 0, "recv: r0 = sender pid");
    CHECK((int32_t)m.r0 == zuzu_getpid(), "sender pid is this process (thread sender)");
    CHECK(m.r1 == 0x51 && m.r2 == 0x52 && m.r3 == 0x53, "send payload w1..w3 intact");
    zuzu_tjoin(t);
    CHECK(g_worker_ok, "sender saw rc=0");

    /* call/reply round trip */
    g_worker_ok = 0;
    t = zuzu_tmake(call_worker, (char *)st + STACK_SIZE, NULL);
    m = zuzu_msg_recv(g_port, TIMEOUT_INFINITE);
    CHECK((int32_t)m.r0 >= 0, "recv of call: r0 = reply handle");
    CHECK((int32_t)m.r1 == zuzu_getpid(), "recv of call: r1 = caller pid");
    CHECK(m.r2 == 0x11 && m.r3 == 0x22, "recv of call: r2/r3 = payload w1/w2");
    CHECK_EQ(zuzu_msg_reply((handle_t)m.r0, 0xA1, 0xA2, 0xA3), 0, "reply");
    zuzu_tjoin(t);
    CHECK(g_worker_ok, "caller got reply words r1..r3");

    /* lsend/lcall/lreply round trip + volatile-buffer contract */
    g_worker_ok = 0;
    t = zuzu_tmake(lcall_worker, (char *)st + STACK_SIZE, NULL);
    m = zuzu_msg_recv(g_port, TIMEOUT_INFINITE);
    char early[sizeof(REQ)];
    lmsg_read(early, sizeof(REQ));            /* FIRST - before any printf */
    CHECK((int32_t)m.r0 >= 0, "recv got the lcall");
    CHECK(m.r2 == sizeof(REQ), "lcall payload length");
    CHECK(memcmp(early, REQ, sizeof(REQ)) == 0, "lmsg payload intact when read first");
    printf("      (volatile-buffer probe: this printf reuses the lmsg buffer)\n");
    char late[sizeof(REQ)];
    lmsg_read(late, sizeof(REQ));
    CHECK(memcmp(late, REQ, sizeof(REQ)) != 0,
          "VOLATILE CONTRACT: printf clobbered lmsg buffer - must lmsg_read before printing");
    lmsg_write(RESP, sizeof(RESP));
    CHECK_EQ(zuzu_msg_lreply((handle_t)m.r0, sizeof(RESP)), 0, "lreply");
    zuzu_tjoin(t);
    CHECK(g_worker_ok, "lcall caller got reply payload intact");

    /* lmsg oversize */
    CHECK_EQ(zuzu_msg_lsend(g_port, LMSG_BUF_SIZE + 1), ERR_OVERFLOW,
             "lsend len>512 -> ERR_OVERFLOW");

    /* type confusion */
    int32_t nt = zuzu_ntfn_create();
    CHECK(nt >= 0, "ntfn_create (type probe)");
    CHECK_EQ(zuzu_msg_send(nt, 1, 2, 3), ERR_BADTYPE, "send on ntfn handle -> ERR_BADTYPE");
    CHECK_EQ(zuzu_msg_send(9999, 1, 2, 3), ERR_BADHANDLE, "send on bogus handle -> ERR_BADHANDLE");

    /* waitany over [port, ntfn] */
    handle_t set[2] = { g_port, (handle_t)nt };
    waitany_result_t res;

    CHECK_EQ(zuzu_waitany(set, 2, TIMEOUT_POLL, &res), ERR_TIMEOUT,
             "waitany poll empty -> ERR_TIMEOUT");

    CHECK_EQ(zuzu_ntfn_signal(nt, 0x9), 0, "signal ntfn");
    CHECK_EQ(zuzu_waitany(set, 2, TIMEOUT_POLL, &res), 0, "waitany: ntfn delivers");
    CHECK(res.kind == WAITANY_KIND_NTFN, "waitany ntfn: kind=NTFN");
    CHECK(res.matched_index == 1, "waitany ntfn: matched_index=1");
    CHECK(res.r1 == 0x9, "waitany ntfn: bits in r1");

    g_worker_ok = 0;
    t = zuzu_tmake(send_worker, (char *)st + STACK_SIZE, NULL);
    CHECK_EQ(zuzu_waitany(set, 2, TIMEOUT_INFINITE, &res), 0, "waitany: port delivers");
    CHECK(res.kind == WAITANY_KIND_SEND, "waitany send: kind=SEND");
    CHECK(res.matched_index == 0, "waitany send: matched_index=0");
    CHECK((int32_t)res.source == zuzu_getpid(), "waitany send: source = sender pid");
    CHECK(res.r1 == 0x51 && res.r2 == 0x52 && res.r3 == 0x53, "waitany send: payload");
    zuzu_tjoin(t);

    /* timed waitany expiry. Frozen ABI nuance: only POLL returns
     * ERR_TIMEOUT; a timed wait that expires returns 0 with
     * kind=TIMEOUT / matched_index=WAITANY_NO_MATCH (syscall_nums.h). */
    uint32_t t0 = uptime_ms();
    CHECK_EQ(zuzu_waitany(set, 2, 50, &res), 0, "waitany timed expiry -> rc 0");
    CHECK(res.kind == WAITANY_KIND_TIMEOUT, "waitany timed expiry: kind=TIMEOUT");
    CHECK(res.matched_index == WAITANY_NO_MATCH, "waitany timed expiry: no match index");
    CHECK(uptime_ms() - t0 >= 50, "waitany timed expiry honored >=50ms");

    CHECK_EQ(zuzu_destroy(nt), 0, "destroy probe ntfn");

    /* destroy port with a blocked waiter -> waiter wakes ERR_DEAD */
    g_worker_ok = 0;
    t = zuzu_tmake(recv_dead_worker, (char *)st + STACK_SIZE, NULL);
    zuzu_sleep(20);   /* let the worker block in recv */
    CHECK_EQ(zuzu_destroy(g_port), 0, "destroy port with blocked receiver");
    zuzu_tjoin(t);
    CHECK(g_worker_ok, "blocked receiver woke with ERR_DEAD");
    g_port = -1;

    zuzu_memunmap(st);
}

static void sec_handles(void)
{
    section("handles");

    /* port create -> use -> destroy; double destroy */
    handle_t p = zuzu_port_create();
    CHECK(p >= 0, "port_create");
    CHECK_EQ(zuzu_destroy(p), 0, "port destroy");
    CHECK_EQ(zuzu_destroy(p), ERR_BADHANDLE, "destroy freed slot -> ERR_BADHANDLE");

    /* ntfn semantics */
    int32_t n = zuzu_ntfn_create();
    CHECK(n >= 0, "ntfn_create");
    CHECK_EQ(zuzu_ntfn_wait(n, TIMEOUT_POLL), ERR_TIMEOUT, "ntfn poll empty -> ERR_TIMEOUT");
    CHECK_EQ(zuzu_ntfn_signal(n, 0x5), 0, "ntfn signal 0x5");
    CHECK_EQ(zuzu_ntfn_signal(n, 0x2), 0, "ntfn signal 0x2 (accumulates)");
    CHECK_EQ(zuzu_ntfn_wait(n, TIMEOUT_POLL), 0x7, "ntfn wait returns OR of bits");
    CHECK_EQ(zuzu_ntfn_wait(n, TIMEOUT_POLL), ERR_TIMEOUT, "bits cleared on delivery");
    CHECK_EQ(zuzu_ntfn_signal(n, 1u << 31), ERR_BADARG, "signal bit 31 -> ERR_BADARG");
    uint32_t t0 = uptime_ms();
    CHECK_EQ(zuzu_ntfn_wait(n, 50), ERR_TIMEOUT, "timed ntfn wait empty -> ERR_TIMEOUT");
    CHECK(uptime_ms() - t0 >= 50, "ntfn timed wait honored >=50ms");
    CHECK_EQ(zuzu_ntfn_signal(n, 0), 0, "signal bits=0 accepted (no-op)");
    CHECK_EQ(zuzu_destroy(n), 0, "ntfn destroy");
    /* Freed-but-in-range slot: the type check fires before liveness, so
     * non-destroy syscalls report ERR_BADTYPE (destroy itself special-cases
     * FREE to ERR_BADHANDLE). Inconsistent but frozen; flagged in review. */
    CHECK_EQ(zuzu_ntfn_signal(n, 1), ERR_BADTYPE, "signal after destroy -> ERR_BADTYPE (freed slot)");

    /* destroy REPLY handle -> rejected, and the handle still works after */
    g_port = zuzu_port_create();
    CHECK(g_port >= 0, "port_create (reply probe)");
    void *st = stack_alloc();
    CHECK(st != NULL, "worker stack");
    g_worker_ok = 0;
    tid_t t = zuzu_tmake(lcall_worker, (char *)st + STACK_SIZE, NULL);
    msg_t m = zuzu_msg_recv(g_port, TIMEOUT_INFINITE);
    char sink[sizeof(REQ)];
    lmsg_read(sink, sizeof(REQ));
    CHECK((int32_t)m.r0 >= 0, "recv reply handle");
    CHECK_EQ(zuzu_destroy((handle_t)m.r0), ERR_BADTYPE, "destroy REPLY handle -> ERR_BADTYPE");
    lmsg_write(RESP, sizeof(RESP));
    CHECK_EQ(zuzu_msg_lreply((handle_t)m.r0, sizeof(RESP)), 0, "reply handle survives destroy attempt");
    zuzu_tjoin(t);
    CHECK(g_worker_ok, "caller unaffected");

    /* destroy TASK handle -> rejected */
    tspawn_result_t ts = zuzu_pspawn("zzt_dummy");
    CHECK(ts.task_handle >= 0, "pspawn empty process");
    CHECK_EQ(zuzu_destroy(ts.task_handle), ERR_BADTYPE, "destroy TASK handle -> ERR_BADTYPE");
    CHECK_EQ(zuzu_pkill(ts.task_handle), 0, "pkill empty process");

    /* cross-process grant: child sends on a port we granted it */
    zpid_t self = zuzu_getpid();
    child_t c;
    int32_t rc = child_spawn("sendport", g_port, &c);
    CHECK_EQ(rc, 0, "spawn sendport child (grant pre-kickstart)");
    if (rc == 0) {
        m = zuzu_msg_recv(g_port, TIMEOUT_INFINITE);
        CHECK((int32_t)m.r0 > 0 && (int32_t)m.r0 != self, "recv: sender is the child pid");
        CHECK(m.r1 == 0xCAFE && m.r2 == 0xBEEF && m.r3 == 0x1234,
              "granted port usable in child (payload intact)");
        int32_t status = -1;
        CHECK(zuzu_wait(c.pid, &status, 0) == c.pid && status == 0, "sendport child exit 0");
    }
    CHECK_EQ(zuzu_destroy(g_port), 0, "destroy grant-probe port");
    g_port = -1;

    /* cross-process shm: parent pattern -> child verifies+rewrites -> parent verifies */
    handle_t sh = zuzu_shm_create(4096);
    CHECK(sh >= 0, "shm_create (cross-proc)");
    uint8_t *pm = (uint8_t *)zuzu_memmap(sh, 0, VM_PROT_RW, 0);
    CHECK(!zuzu_is_err(pm), "parent maps shm");
    for (uint32_t i = 0; i < 4096; i++) pm[i] = (uint8_t)(0xA5 + i);
    int32_t status = -1;
    rc = child_run("shm", sh, &status);
    CHECK_EQ(rc, 0, "spawn+wait shm child");
    CHECK_EQ(status, 0, "child saw parent's pattern and rewrote (exit 0)");
    int ok = 1;
    for (uint32_t i = 0; i < 4096; i++)
        if (pm[i] != (uint8_t)(0x5A ^ i)) { ok = 0; break; }
    CHECK(ok, "parent sees child's writes (shared mapping)");
    CHECK_EQ(zuzu_memunmap(pm), 0, "parent unmaps shm");
    CHECK_EQ(zuzu_destroy(sh), 0, "cross-proc shm destroyed");

    /* Lifetime ordering: A creates + grants to B, A destroys its OWN handle
     * while B still holds one, then B must still read intact data (no UAF),
     * and B's exit drops the last reference (no leak). */
    handle_t sh2 = zuzu_shm_create(4096);
    CHECK(sh2 >= 0, "shm_create (lifetime ordering)");
    uint8_t *pm2 = (uint8_t *)zuzu_memmap(sh2, 0, VM_PROT_RW, 0);
    CHECK(!zuzu_is_err(pm2), "parent maps (lifetime ordering)");
    for (uint32_t i = 0; i < 4096; i++) pm2[i] = (uint8_t)(0xA5 + i);
    CHECK_EQ(zuzu_memunmap(pm2), 0, "parent unmaps before grant+destroy");
    child_t c2;
    int32_t rc2 = child_spawn("shm", sh2, &c2);   /* grants sh2 -> ref 2 */
    CHECK_EQ(rc2, 0, "spawn shm child (lifetime ordering)");
    if (rc2 == 0) {
        CHECK_EQ(zuzu_destroy(sh2), 0,
                 "A destroys its shm handle while child still holds one (ref 2->1)");
        int32_t status2 = -1;
        CHECK(zuzu_wait(c2.pid, &status2, 0) == c2.pid, "wait shm-ordering child");
        CHECK_EQ(status2, 0,
                 "B reads intact data after A's destroy (no UAF); B exit frees object");
    }

    zuzu_memunmap(st);
}

static void sec_tasks(void)
{
    section("tasks");

    /* signedness regression: error returns must be detectable as < 0.
     * NOTE: entry must be OUT OF RANGE (>= USER_VA_TOP). validate_user_ptr
     * is range-only, so tmake(NULL) actually spawns a thread at pc=0 whose
     * prefetch abort kills the whole process — flagged as a kernel finding,
     * deliberately not exercised here. */
    tid_t bad = zuzu_tmake((void (*)(void *))0x90000000u, NULL, NULL);
    CHECK(bad < 0, "tid_t is signed: tmake(bad entry) < 0");
    CHECK_EQ(bad, ERR_BADPTR, "tmake kernel-range entry -> ERR_BADPTR");
    int32_t jrc = zuzu_tjoin((tid_t)999999);
    CHECK(jrc < 0, "tjoin error detectable as < 0");
    CHECK_EQ(jrc, ERR_NOENT, "tjoin bogus tid -> ERR_NOENT");
    int32_t dummy;
    CHECK_EQ(zuzu_wait(-5, &dummy, 0), ERR_BADARG, "wait(pid<-1) -> ERR_BADARG");
    int32_t wrc = zuzu_wait(29999, &dummy, 0);
    CHECK(wrc < 0, "zpid_t is signed: wait error < 0");
    CHECK_EQ(wrc, ERR_NOENT, "wait on non-child pid -> ERR_NOENT");
    CHECK(zuzu_getpid() > 0, "getpid > 0");

    /* tmake -> tjoin exit status */
    void *st = stack_alloc();
    CHECK(st != NULL, "worker stack");
    g_spin_quit[1] = 0;
    tid_t t = zuzu_tmake(spin_worker, (char *)st + STACK_SIZE, (void *)1);
    CHECK(t > 0, "tmake spin worker");
    g_spin_quit[1] = 1;
    CHECK_EQ(zuzu_tjoin(t), 0x41, "tjoin returns exit status");

    /* TCB slot exhaustion: main holds 1 of TCB_MAX_SLOTS(7) -> 6 workers max.
     * Quiesce first: joined threads' TCB slots are released by the deferred
     * reaper, so slots from earlier sections may still be held briefly. */
    zuzu_sleep(20);
    void *stacks[TCB_MAX_SLOTS];
    tid_t tids[TCB_MAX_SLOTS];
    int made = 0;
    for (int i = 0; i < TCB_MAX_SLOTS - 1; i++) {
        stacks[i] = stack_alloc();
        if (!stacks[i]) break;
        g_spin_quit[i] = 0;
        tids[i] = zuzu_tmake(spin_worker, (char *)stacks[i] + STACK_SIZE,
                             (void *)(uintptr_t)i);
        if (tids[i] < 0) break;
        made++;
    }
    CHECK(made == TCB_MAX_SLOTS - 1, "created TCB_MAX_SLOTS-1 (6) worker threads");
    void *xs = stack_alloc();
    CHECK(xs != NULL, "stack for overflow probe");
    tid_t over = zuzu_tmake(spin_worker, (char *)xs + STACK_SIZE, (void *)6);
    CHECK_EQ(over, ERR_NOMEM, "tmake past TCB_MAX_SLOTS -> ERR_NOMEM");
    if (over > 0) { /* defensive: don't leave a stray spinner if it slipped in */
        g_spin_quit[6] = 1;
        zuzu_tjoin(over);
    }
    /* join one -> slot frees -> tmake succeeds again */
    g_spin_quit[0] = 1;
    CHECK_EQ(zuzu_tjoin(tids[0]), 0x40, "join frees a slot");
    zuzu_sleep(10);   /* deferred thread reaper releases the TCB slot */
    g_spin_quit[6] = 0;
    tid_t again = zuzu_tmake(spin_worker, (char *)xs + STACK_SIZE, (void *)6);
    CHECK(again > 0, "tmake succeeds again after join");
    g_spin_quit[6] = 1;
    zuzu_tjoin(again);
    for (int i = 1; i < made; i++) {
        g_spin_quit[i] = 1;
        zuzu_tjoin(tids[i]);
    }
    zuzu_sleep(10);
    for (int i = 0; i < made; i++) zuzu_memunmap(stacks[i]);
    zuzu_memunmap(xs);
    zuzu_memunmap(st);

    /* pspawn -> kickstart -> wait lifecycle through sysd exec */
    int32_t status = -1;
    CHECK_EQ(child_run("exit0", -1, &status), 0, "spawn/wait exit0 child");
    CHECK_EQ(status, 0, "exit status 0 propagated");
    status = -1;
    CHECK_EQ(child_run("exit42", -1, &status), 0, "spawn/wait exit42 child");
    CHECK_EQ(status, 42, "exit status 42 propagated");
    status = -1;
    CHECK_EQ(child_run("dirty", -1, &status), 0, "spawn/wait dirty child (live thread+shm+port at exit)");
    CHECK_EQ(status, 7, "dirty child pquit(7) status propagated");

    /* timeout convention: sleep honors ms */
    uint32_t t0 = uptime_ms();
    CHECK_EQ(zuzu_sleep(100), 0, "sleep(100) rc 0");
    uint32_t dt = uptime_ms() - t0;
    CHECK(dt >= 100, "sleep(100) slept >= 100ms");
    CHECK(dt < 1000, "sleep(100) did not oversleep grossly");
}

static void sec_vfp(void)
{
    section("vfp");

    /* Upper-bank VFP (d16-d31) context-switch integrity. Two child
     * processes spin write-sentinel -> yield -> readback -> assert with
     * distinct per-process patterns (see zztest_child mode_vfp); with the
     * parent blocked in wait they ping-pong on yield, so every iteration
     * crosses a process switch. One assertion catches both a switch path
     * that skips the upper bank and one that swaps state between
     * processes — either way a foreign/stale value reads back. */
    child_t ca, cb;
    int32_t ra = child_spawn("vfpa", -1, &ca);
    CHECK_EQ(ra, 0, "spawn vfpa child");
    int32_t rb = child_spawn("vfpb", -1, &cb);
    CHECK_EQ(rb, 0, "spawn vfpb child");

    int32_t status;
    if (ra == 0) {
        status = -1;
        CHECK(zuzu_wait(ca.pid, &status, 0) == ca.pid, "wait vfpa child");
        CHECK_EQ(status, 0, "vfpa: d16/d31 sentinels survive 400 yields");
    }
    if (rb == 0) {
        status = -1;
        CHECK(zuzu_wait(cb.pid, &status, 0) == cb.pid, "wait vfpb child");
        CHECK_EQ(status, 0, "vfpb: d16/d31 sentinels survive 400 yields");
    }
}

static void sec_version(void)
{
    section("version");

    int32_t n = zuzu_ntfn_create();
    handle_t set[1] = { (handle_t)n };
    waitany_result_t res;

    /* too-small size must be rejected before anything else happens */
    memset(&res, 0, sizeof(res));
    res.size = sizeof(waitany_result_t) - 4;
    CHECK_EQ(raw_waitany(set, 1, TIMEOUT_POLL, &res), ERR_BADARG,
             "waitany result.size too small -> ERR_BADARG");
    res.size = 0;
    CHECK_EQ(raw_waitany(set, 1, TIMEOUT_POLL, &res), ERR_BADARG,
             "waitany result.size=0 -> ERR_BADARG");

    /* correct size accepted (raw svc, no wrapper help) */
    zuzu_ntfn_signal(n, 0x3);
    memset(&res, 0, sizeof(res));
    res.size = sizeof(waitany_result_t);
    CHECK_EQ(raw_waitany(set, 1, TIMEOUT_POLL, &res), 0,
             "waitany correct result.size -> success");
    CHECK(res.kind == WAITANY_KIND_NTFN && res.r1 == 0x3,
          "result delivered through raw svc");
    zuzu_destroy(n);
}

static void sec_security(void)
{
    section("security");

    /* --- confused-deputy: kernel must never read/write a kernel address
     * on the caller's behalf. All go through validate_user_ptr. --- */
    int32_t nt = zuzu_ntfn_create();
    CHECK(nt >= 0, "ntfn_create (probe)");
    handle_t one[1] = { (handle_t)nt };

    CHECK_EQ(raw_waitany(one, 1, TIMEOUT_POLL, (waitany_result_t *)0x90000000u), ERR_BADPTR,
             "waitany result ptr in kernel range -> ERR_BADPTR (no kernel write)");
    waitany_result_t vr; vr.size = sizeof(vr);
    CHECK_EQ(raw_waitany((handle_t *)0x90000000u, 1, TIMEOUT_POLL, &vr), ERR_BADPTR,
             "waitany handles ptr in kernel range -> ERR_BADPTR (no kernel read)");

    /* --- waitany argument bounds (DoS / OOB guards) --- */
    waitany_result_t res;
    CHECK_EQ(zuzu_waitany(NULL, 1, TIMEOUT_POLL, &res), ERR_BADARG,
             "waitany NULL handles -> ERR_BADARG");
    CHECK_EQ(zuzu_waitany(one, 0, TIMEOUT_POLL, &res), ERR_BADARG,
             "waitany count=0 -> ERR_BADARG");
    CHECK_EQ(zuzu_waitany(one, 17, TIMEOUT_POLL, &res), ERR_BADARG,
             "waitany count>WAITANY_MAX(16) -> ERR_BADARG");
    handle_t sh = zuzu_shm_create(4096);
    CHECK(sh >= 0, "shm_create (waitany type probe)");
    handle_t badset[1] = { sh };
    CHECK_EQ(zuzu_waitany(badset, 1, TIMEOUT_POLL, &res), ERR_BADTYPE,
             "waitany on non-waitable (shm) handle -> ERR_BADTYPE");
    CHECK_EQ(zuzu_destroy(sh), 0, "destroy waitany-probe shm");
    CHECK_EQ(zuzu_destroy(nt), 0, "destroy waitany-probe ntfn");

    /* --- privilege gate: asinject is init-only --- */
    uint8_t src[64];
    CHECK_EQ(zuzu_asinject(0, 0x10000000u, src, sizeof(src), VM_PROT_READ), ERR_NOPERM,
             "asinject from unprivileged process -> ERR_NOPERM");

    /* --- kickstart / pkill type confinement: can't drive arbitrary objects --- */
    handle_t kp = zuzu_port_create();
    CHECK(kp >= 0, "port_create (kickstart/pkill probe)");
    CHECK_EQ(zuzu_kickstart(kp, 0x10000u, 0x1000u, 0, 0), ERR_BADTYPE,
             "kickstart on port handle -> ERR_BADTYPE");
    CHECK_EQ(zuzu_kickstart(9999, 0x10000u, 0x1000u, 0, 0), ERR_BADHANDLE,
             "kickstart on bogus handle -> ERR_BADHANDLE");
    CHECK_EQ(zuzu_pkill(kp), ERR_BADTYPE, "pkill on port handle -> ERR_BADTYPE");
    CHECK_EQ(zuzu_pkill(9999), ERR_BADHANDLE, "pkill on bogus handle -> ERR_BADHANDLE");

    /* --- grant authority checks --- */
    CHECK_EQ(zuzu_grant(kp, 999999), ERR_NOENT, "grant to nonexistent pid -> ERR_NOENT");
    CHECK_EQ(zuzu_grant(9999, g_sysd_pid), ERR_BADHANDLE, "grant bogus handle -> ERR_BADHANDLE");
    CHECK_EQ(zuzu_destroy(kp), 0, "destroy grant-probe port");

    /* --- reply-cap forgery / replay --- */
    g_port = zuzu_port_create();
    CHECK(g_port >= 0, "port_create (reply-forgery probe)");
    void *st = stack_alloc();
    CHECK(st != NULL, "worker stack");
    g_worker_ok = 0;
    tid_t t = zuzu_tmake(lcall_worker, (char *)st + STACK_SIZE, NULL);
    msg_t m = zuzu_msg_recv(g_port, TIMEOUT_INFINITE);
    char sink[sizeof(REQ)];
    lmsg_read(sink, sizeof(REQ));
    CHECK((int32_t)m.r0 >= 0, "recv reply handle");
    CHECK_EQ(zuzu_msg_reply(g_port, 0, 0, 0), ERR_BADTYPE,
             "reply on a port handle -> ERR_BADTYPE (no cap forgery from endpoint)");
    CHECK_EQ(zuzu_msg_reply(9999, 0, 0, 0), ERR_BADHANDLE,
             "reply on bogus handle -> ERR_BADHANDLE");
    CHECK_EQ(zuzu_msg_reply(0, 0, 0, 0), ERR_BADHANDLE,
             "reply on handle 0 -> ERR_BADHANDLE");
    CHECK_EQ(zuzu_grant((handle_t)m.r0, g_sysd_pid), ERR_NOPERM,
             "cannot grant a REPLY handle (no cap leak)");
    lmsg_write(RESP, sizeof(RESP));
    CHECK_EQ(zuzu_msg_lreply((handle_t)m.r0, sizeof(RESP)), 0, "genuine reply succeeds");
    CHECK_EQ(zuzu_msg_reply((handle_t)m.r0, 0, 0, 0), ERR_BADTYPE,
             "double-reply on spent reply handle -> ERR_BADTYPE (no replay)");
    zuzu_tjoin(t);
    CHECK(g_worker_ok, "caller unaffected by forgery attempts");
    CHECK_EQ(zuzu_destroy(g_port), 0, "destroy reply-forgery probe port");
    g_port = -1;

    /* --- wait must not write status to a kernel pointer --- */
    child_t c;
    int32_t rc = child_spawn("exit0", -1, &c);
    CHECK_EQ(rc, 0, "spawn exit0 child (bad-status-ptr probe)");
    if (rc == 0) {
        CHECK_EQ(zuzu_wait(c.pid, (int32_t *)0x90000000u, 0), ERR_BADPTR,
                 "wait with kernel-range status ptr -> ERR_BADPTR (no kernel write)");
        int32_t status = -1;
        CHECK(zuzu_wait(c.pid, &status, 0) == c.pid && status == 0,
              "child still reapable with a valid status ptr");
    }

    /* --- cross-process capability confinement: a received cap cannot be
     * re-granted or destroyed by the receiver, but still works. --- */
    g_port = zuzu_port_create();
    CHECK(g_port >= 0, "port_create (confinement probe)");
    rc = child_spawn("regrant", g_port, &c);
    CHECK_EQ(rc, 0, "spawn regrant child (grant pre-kickstart)");
    if (rc == 0) {
        m = zuzu_msg_recv(g_port, TIMEOUT_INFINITE);  /* unblocks child's send */
        CHECK(m.r1 == 0xF00D, "received cap still usable for its purpose (send)");
        int32_t status = -1;
        CHECK(zuzu_wait(c.pid, &status, 0) == c.pid && status == 0,
              "child: re-grant AND destroy of received cap both refused (ERR_NOPERM)");
    }
    CHECK_EQ(zuzu_destroy(g_port), 0, "destroy confinement-probe port");
    g_port = -1;

    zuzu_memunmap(st);
}

static void leak_loop_anon(void)
{
    uint8_t *a = (uint8_t *)zuzu_memmap(HANDLE_ANON, 8192, VM_PROT_RW, 0);
    if (!zuzu_is_err(a)) {
        a[0] = 1; a[8191] = 2;   /* fault both pages in */
        zuzu_memunmap(a);
    }
}

static void leak_loop_shm(void)
{
    handle_t sh = zuzu_shm_create(4096);
    if (sh < 0) return;
    uint8_t *m = (uint8_t *)zuzu_memmap(sh, 0, VM_PROT_RW, 0);
    if (!zuzu_is_err(m)) {
        m[0] = 1;                /* fault the page in */
        zuzu_memunmap(m);
    }
    zuzu_destroy(sh);
}

static void leak_loop_thread(void)
{
    void *st = stack_alloc();
    if (!st) return;
    tid_t t = zuzu_tmake(trivial_worker, (char *)st + STACK_SIZE, NULL);
    if (t > 0) zuzu_tjoin(t);
    zuzu_sleep(5);               /* deferred reaper frees kstack+TCB slot */
    zuzu_memunmap(st);
}

static void leak_loop_spawn(int i)
{
    int32_t status;
    (void)child_run((i & 1) ? "dirty" : "exit0", -1, &status);
}

static void sec_leaks(void)
{
    section("leaks");

    uint32_t base;

    /* anon map/unmap */
    leak_loop_anon();                      /* warm-up */
    zuzu_sleep(10);
    base = pages_free();
    for (int i = 0; i < LEAK_ITERS; i++) leak_loop_anon();
    zuzu_sleep(10);
    CHECK_EQ((int32_t)(pages_free() - base), 0, "anon x50: free pages back to baseline");

    /* shm full lifecycle */
    leak_loop_shm();                       /* warm-up */
    zuzu_sleep(10);
    base = pages_free();
    for (int i = 0; i < LEAK_ITERS; i++) leak_loop_shm();
    zuzu_sleep(10);
    CHECK_EQ((int32_t)(pages_free() - base), 0, "shm lifecycle x50: free pages back to baseline");

    /* thread create/join */
    leak_loop_thread();                    /* warm-up */
    zuzu_sleep(10);
    base = pages_free();
    for (int i = 0; i < LEAK_ITERS; i++) leak_loop_thread();
    zuzu_sleep(10);
    CHECK_EQ((int32_t)(pages_free() - base), 0, "thread x50: free pages back to baseline");

    /* spawn/exit, alternating clean and dirty children */
    leak_loop_spawn(0);                    /* warm-up */
    leak_loop_spawn(1);                    /* warm up the dirty path too */
    zuzu_sleep(10);
    base = pages_free();
    for (int i = 0; i < LEAK_ITERS; i++) leak_loop_spawn(i);
    zuzu_sleep(10);
    CHECK_EQ((int32_t)(pages_free() - base), 0, "spawn x50 (25 clean + 25 dirty): free pages back to baseline");
}

/* ---------------- main ---------------- */

int main(void)
{
    printf("zztest: Loaf ABI regression suite\n");

    if (sysd_setup() != 0) {
        printf("FATAL: sysd lookup failed, cannot run cross-process tests\n");
        return 127;
    }

    sec_mem();
    sec_ipc();
    sec_handles();
    sec_tasks();
    sec_vfp();
    sec_version();
    sec_security();
    sec_leaks();

    int total_pass = 0, total_fail = 0;
    printf("\n===== zztest summary =====\n");
    printf("%-10s %6s %6s\n", "section", "pass", "fail");
    for (int i = 0; i <= cur_sec; i++) {
        printf("%-10s %6d %6d\n", sections[i].name, sections[i].pass, sections[i].fail);
        total_pass += sections[i].pass;
        total_fail += sections[i].fail;
    }
    printf("%-10s %6d %6d\n", "TOTAL", total_pass, total_fail);
    printf("%s (%d failures)\n", total_fail ? "FAILED" : "ALL PASS", total_fail);
    return total_fail;
}
