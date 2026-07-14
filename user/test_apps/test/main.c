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

    printf("tid = %d lmsg_buf = %p", __zuzu_tcb()->tid, lmsg_buf());

    lmsg_write(REQ, sizeof(REQ));
    msg_t r = _lcall(port, sizeof(REQ));

    if ((int32_t)r.r0 == 0) {
        lmsg_read(buf, r.r1);              /* r1 = reply length */
        worker_ok = (r.r1 == sizeof(RESP) &&
                     memcmp(buf, RESP, sizeof(RESP)) == 0);
    }
    worker_done = 1;
    _tquit(0);
}

int main(void)
{
    port = _port_create();
    CHECK(port >= 0, "port_create");

    void *stack = _memmap(HANDLE_ANON, STACK_SIZE, VM_PROT_READ | VM_PROT_WRITE, 0);
    CHECK(!_ptr_is_err(stack), "worker stack");

    tid_t tid = _tmake(worker, (char *)stack + STACK_SIZE, NULL);

    msg_t m = _recv_timeout(port, TIMEOUT_INFINITE);
    char got[LMSG_BUF_SIZE];
    uint32_t len = m.r2;
    lmsg_read(got, len);              /* FIRST — before any printf */

    CHECK((int32_t)m.r0 >= 0, "recv got the lcall");
    CHECK(len == sizeof(REQ), "payload length matches");
    CHECK(memcmp(got, REQ, sizeof(REQ)) == 0, "worker's lmsg payload arrived intact");
    printf("got: '%s' (first bytes %02x %02x %02x %02x)\n",
        got, got[0], got[1], got[2], got[3]);
    printf("main lmsg_buf VA = %p, tid=%u pid=%u\n",
        lmsg_buf(), __zuzu_tcb()->tid, __zuzu_tcb()->pid);

    lmsg_write(RESP, sizeof(RESP));
    CHECK(_lreply((handle_t)m.r0, sizeof(RESP)) == 0, "lreply");

    _tjoin(tid);
    CHECK(worker_ok, "worker received the lreply payload intact");
    

    lmsg_write(REQ, sizeof(REQ));
    CHECK((int32_t)_lsend(port, LMSG_BUF_SIZE + 1) < 0, "lsend len>512 rejected");

    CHECK(_memunmap((void *)SYSPAGE_VA) == ERR_NOPERM, "syspage unmap refused");
    CHECK(_memunmap((void *)((uintptr_t)__zuzu_tcb() & ~0xFFFu)) == ERR_NOPERM, "TCB page unmap refused");
    CHECK(_mprotect((void *)SYSPAGE_VA, PAGE_SIZE, VM_PROT_READ | VM_PROT_WRITE) != 0, "syspage mprotect refused");

    /* force multiple sbrk growths + tail donation */
    void *p[200];
    for (int i = 0; i < 200; i++) { p[i] = malloc(1024); CHECK(p[i] != NULL, "..."); }
    for (int i = 0; i < 200; i++) free(p[i]);
    void *big = malloc(100000);   /* > ARENA_CHUNK_SIZE, single sbrk */
    CHECK(big != NULL, "large alloc");
    free(big);

    _memunmap(stack);
    _destroy(port);

    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails;
}