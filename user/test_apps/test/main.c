#include <zuzu/types.h>
#include <zuzu/task.h>
#include <zuzu/msg.h>  
#include <zuzu/lmsg.h>
#include <zuzu/umem.h>
#include <zuzu/tcb.h>
#include <zuzu/syspage.h>
#include <malloc.h>
#include <mem.h>
#include <stdio.h>

#define STACK_SIZE 4096
#define REQ  "hello from worker thread"
#define RESP "reply from main thread"

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); fails++; } \
                         else      { printf("ok:   %s\n", m); } } while (0)

static handle_t port;
static volatile int worker_done = 0;
static volatile int worker_ok   = 0;

static void worker(void *arg)
{
    (void)arg;
    char buf[LMSG_BUF_SIZE];

    printf("tid = %d lmsg_buf = %p", zuzu_tcb()->tid, lmsg_buf());

    lmsg_write(REQ, sizeof(REQ));
    msg_t r = zuzu_msg_lcall(port, sizeof(REQ));

    if ((int32_t)r.r0 == 0) {
        lmsg_read(buf, r.r1);              /* r1 = reply length */
        worker_ok = (r.r1 == sizeof(RESP) &&
                     memcmp(buf, RESP, sizeof(RESP)) == 0);
    }
    worker_done = 1;
    zuzu_tquit(0);
}

int main(void)
{
    port = zuzu_port_create();
    CHECK(port >= 0, "port_create");

    void *stack = zuzu_memmap(HANDLE_ANON, STACK_SIZE, VM_PROT_READ | VM_PROT_WRITE, 0);
    CHECK(!zuzu_is_err(stack), "worker stack");

    tid_t tid = zuzu_tmake(worker, (char *)stack + STACK_SIZE, NULL);

    msg_t m = zuzu_msg_recv(port, TIMEOUT_INFINITE);
    char got[LMSG_BUF_SIZE];
    uint32_t len = m.r2;
    lmsg_read(got, len);              /* FIRST — before any printf */

    CHECK((int32_t)m.r0 >= 0, "recv got the lcall");
    CHECK(len == sizeof(REQ), "payload length matches");
    CHECK(memcmp(got, REQ, sizeof(REQ)) == 0, "worker's lmsg payload arrived intact");
    printf("got: '%s' (first bytes %02x %02x %02x %02x)\n",
        got, got[0], got[1], got[2], got[3]);
    printf("main lmsg_buf VA = %p, tid=%u pid=%u\n",
        lmsg_buf(), zuzu_tcb()->tid, zuzu_tcb()->pid);

    lmsg_write(RESP, sizeof(RESP));
    CHECK(zuzu_msg_lreply((handle_t)m.r0, sizeof(RESP)) == 0, "lreply");

    zuzu_tjoin(tid);
    CHECK(worker_ok, "worker received the lreply payload intact");
    

    lmsg_write(REQ, sizeof(REQ));
    CHECK((int32_t)zuzu_msg_lsend(port, LMSG_BUF_SIZE + 1) < 0, "lsend len>512 rejected");

    CHECK(zuzu_memunmap((void *)SYSPAGE_VA) == ERR_NOPERM, "syspage unmap refused");
    CHECK(zuzu_memunmap((void *)((uintptr_t)zuzu_tcb() & ~0xFFFu)) == ERR_NOPERM, "TCB page unmap refused");
    CHECK(zuzu_memprotect((void *)SYSPAGE_VA, PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE) != 0, "syspage mprotect refused");

    /* force multiple sbrk growths + tail donation */
    void *p[200];
    void *big = malloc(100000);   /* > ARENA_CHUNK_SIZE, single sbrk */
    CHECK(big != NULL, "large alloc");
    free(big);

    zuzu_memunmap(stack);
    zuzu_destroy(port);

    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails;
}