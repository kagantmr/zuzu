/* zztest_child - helper binary for the zztest ABI regression suite.
 *
 * Spawned by zztest via sysd exec with a mode string in argv[1]:
 *
 *   exit0            return 0 immediately
 *   exit42           return 42 (exit-status propagation)
 *   dirty            allocate anon memory, a port, an ntfn, a mapped shm
 *                    and a live worker thread, then pquit(7) with NO
 *                    cleanup — exercises full kernel teardown of a dirty
 *                    process (leak-guard companion)
 *   shm <slot>       map the granted shm handle in <slot>, verify the
 *                    parent's 0xA5+i pattern, overwrite with 0x5A^i,
 *                    unmap, exit 0 (nonzero exit = which step failed)
 *   sendport <slot>  zuzu_msg_send(0xCAFE, 0xBEEF, 0x1234) on the granted
 *                    port handle in <slot>, exit 0 on success
 *   regrant <slot>   confinement: a *received* (granted) cap must be
 *                    non-grantable and non-destroyable by the receiver.
 *                    Both must be refused (ERR_NOPERM); exit 0 on success.
 *   vfpa / vfpb      upper-bank VFP context-switch integrity: spin writing
 *                    per-process sentinels into d16/d31, yield, read back,
 *                    assert. Two instances with distinct sentinels run
 *                    concurrently; 117 = d16 corrupted, 118 = d31 corrupted.
 *
 * Exit codes 100+ are child-side failures; zztest reports them.
 */
#include <zuzu/zuzu.h>
#include <zuzu/memprot.h>
#include <stdlib.h>
#include <string.h>
#include <mem.h>

#define SHM_TEST_SIZE 4096u

static void dirty_worker(void *arg)
{
    (void)arg;
    for (;;)
        zuzu_sleep(50);
}

static int mode_dirty(void)
{
    void *a = zuzu_memmap(HANDLE_ANON, 8192, VM_PROT_RW, 0);
    if (zuzu_is_err(a))
        return 101;
    memset(a, 0xDD, 8192);

    if (zuzu_port_create() < 0)
        return 102;

    int32_t n = zuzu_ntfn_create();
    if (n < 0)
        return 103;
    (void)zuzu_ntfn_signal(n, 0x1); /* leave a pending signal behind */

    handle_t sh = zuzu_shm_create(SHM_TEST_SIZE);
    if (sh < 0)
        return 104;
    void *m = zuzu_memmap(sh, 0, VM_PROT_RW, 0);
    if (zuzu_is_err(m))
        return 105;
    memset(m, 0xEE, SHM_TEST_SIZE);

    void *stack = zuzu_memmap(HANDLE_ANON, 4096, VM_PROT_RW, 0);
    if (zuzu_is_err(stack))
        return 106;
    if (zuzu_tmake(dirty_worker, (char *)stack + 4096, NULL) < 0)
        return 107;

    zuzu_sleep(5);   /* let the worker reach its sleep loop */
    zuzu_pquit(7);   /* dirty exit: everything above still live */
    return 7;        /* unreachable; pquit does not return */
}

static int mode_shm(handle_t slot)
{
    uint8_t *m = (uint8_t *)zuzu_memmap(slot, 0, VM_PROT_RW, 0);
    if (zuzu_is_err(m))
        return 110;

    for (uint32_t i = 0; i < SHM_TEST_SIZE; i++) {
        if (m[i] != (uint8_t)(0xA5 + i))
            return 111;
    }
    for (uint32_t i = 0; i < SHM_TEST_SIZE; i++)
        m[i] = (uint8_t)(0x5A ^ i);

    if (zuzu_memunmap(m) != 0)
        return 112;
    return 0;
}

static int mode_sendport(handle_t slot)
{
    int32_t rc = zuzu_msg_send(slot, 0xCAFE, 0xBEEF, 0x1234);
    return (rc == 0) ? 0 : 113;
}

static int mode_regrant(handle_t slot)
{
    /* A received capability must not be re-grantable (prevents unbounded
     * propagation), and we must not be able to destroy an object we don't
     * own. Both must be refused with ERR_NOPERM. */
    if (zuzu_grant(slot, NAMETABLE_PID) != ERR_NOPERM)
        return 114;
    if (zuzu_destroy(slot) != ERR_NOPERM)
        return 115;
    /* the cap must still work for its intended use (send) */
    if (zuzu_msg_send(slot, 0xF00D, 0, 0) != 0)
        return 116;
    return 0;
}

/* Upper-bank (d16-d31) registers only exist in the VFP context the kernel
 * saves/restores in context_switch; a switch path that handles just d0-d15
 * corrupts them silently. Write/yield/readback is a single asm block so the
 * compiler cannot cache anything in the probed registers in between; the
 * yield svc clobbers only r0 (same contract as zuzu_yield). Patterns vary
 * per iteration so a stale value from an earlier switch also mismatches. */
static int mode_vfp(uint32_t sent)
{
    zuzu_sleep(20);   /* barrier: let the sibling instance get spawned */

    for (uint32_t i = 0; i < 400; i++) {
        uint32_t a = sent ^ i;
        uint32_t b = ~sent + i;
        uint32_t c = sent + (i * 0x01010101u);
        uint32_t d = ~(sent ^ (i << 16));
        uint32_t ra, rb, rc, rd;
        __asm__ volatile(
            "vmov d16, %[a], %[b]\n\t"
            "vmov d31, %[c], %[d]\n\t"
            "svc %[num]\n\t"
            "vmov %[ra], %[rb], d16\n\t"
            "vmov %[rc], %[rd], d31"
            : [ra] "=&r"(ra), [rb] "=&r"(rb), [rc] "=&r"(rc), [rd] "=&r"(rd)
            : [a] "r"(a), [b] "r"(b), [c] "r"(c), [d] "r"(d),
              [num] "i"(SYS_YIELD)
            : "r0", "memory", "d16", "d31");
        if (ra != a || rb != b)
            return 117;
        if (rc != c || rd != d)
            return 118;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 100;

    const char *mode = argv[1];

    if (strcmp(mode, "exit0") == 0)
        return 0;
    if (strcmp(mode, "exit42") == 0)
        return 42;
    if (strcmp(mode, "dirty") == 0)
        return mode_dirty();
    if (strcmp(mode, "vfpa") == 0)
        return mode_vfp(0xA5A5F00Du);
    if (strcmp(mode, "vfpb") == 0)
        return mode_vfp(0x5A5ABEEFu);

    if (argc >= 3) {
        handle_t slot = (handle_t)strtol(argv[2], NULL, 10);
        if (strcmp(mode, "shm") == 0)
            return mode_shm(slot);
        if (strcmp(mode, "sendport") == 0)
            return mode_sendport(slot);
        if (strcmp(mode, "regrant") == 0)
            return mode_regrant(slot);
    }

    return 100;
}
