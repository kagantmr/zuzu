#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zuzu/types.h>
#include <zuzu/task.h>
#include <zuzu/msg.h>
#include <zuzu/lmsg.h>
#include "globals.h"
#include "pmccntr.h"


/* First payload word (w1) the client sends to ask the echo thread to exit.
 * zuzu_msg_recv() of a call hands the receiver: w0 = reply handle,
 * w1 = caller pid, w2/w3 = the caller's w1/w2 - so the sentinel must be
 * checked against w2, not w1. */
#define MSG_QUIT 0xFFFFFFFFu
#define MSG_PING 0u

/* ZuzuMsgLcall has no w1/w2 word arguments -- the only thing sys_msg_recv
 * hands the receiver for an lcall is the transferred buffer length in w2
 * (see kernel/ipc/sys_ipc.c). LCALL_QUIT_LEN just needs to be distinct from
 * LCALL_PAYLOAD_LEN so the server can tell "shut down" from "echo this". */
#define LCALL_PAYLOAD_LEN 32u
#define LCALL_QUIT_LEN    4u

/* Direct read of the ARMv7 32-bit cycle counter. The PMU is already enabled
 * and opened up for PL0 access at boot (see pmu_init() in arch/arm/early.c),
 * so no setup is needed here. */
static void echo_server_thread(void *arg) {
    Handle port = *(Handle *)arg;

    for (;;) {
        Message cmd = ZuzuMsgRecv(port, TIMEOUT_INFINITE);

        if (cmd.w2 == MSG_QUIT) {
            ZuzuMsgReply(cmd.w0, 1, 0, 0);
            ZuzuTQuit(ZUZU_OK);
        }

        ZuzuMsgReply(cmd.w0, 1, 0, 0);
    }
}

/* Same shape as echo_server_thread, but for ZuzuMsgLcall: reads the
 * caller's buffer, writes it straight back, and replies via
 * ZuzuMsgLreply so the round trip actually exercises the lmsg_buf copy
 * on both legs, not just the register-passing fast path. */
static void lcall_echo_server_thread(void *arg) {
    Handle port = *(Handle *)arg;
    static uint8_t buf[LMSG_BUF_SIZE];

    for (;;) {
        Message cmd = ZuzuMsgRecv(port, TIMEOUT_INFINITE);
        uint32_t len = cmd.w2;

        if (len == LCALL_QUIT_LEN) {
            ZuzuMsgLreply(cmd.w0, 0);
            ZuzuTQuit(ZUZU_OK);
        }

        LmsgRead(buf, len);
        LmsgWrite(buf, len);
        ZuzuMsgLreply(cmd.w0, len);
    }
}

/* Rough cycles-per-microsecond estimate, derived by bracketing a known-length
 * sleep with cycle-counter reads. TICK_HZ is 100 (10ms resolution) so a
 * short calibration window would carry a large relative error; 500ms keeps
 * that error near 1-2%. This is only ever used to make the report readable
 * in real time, not as a precise measurement. */
static uint32_t calibrate_cycles_per_us(void) {
    barrier();
    uint32_t start = read_pmccntr();
    barrier();

    ZuzuSleep(CALIBRATION_MS);

    barrier();
    uint32_t end = read_pmccntr();
    barrier();

    return (end - start) / (CALIBRATION_MS * 1000u);
}

static uint32_t g_samples[BENCHMARK_ITERATIONS];

static void run_benchmark(Handle port) {
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        ZuzuMsgCall(port, MSG_PING, 0, 0);
    }

    uint32_t errors = 0;
    int32_t first_err_r0 = 0;
    uint32_t first_err_r1 = 0;

    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        barrier();
        uint32_t start = read_pmccntr();
        barrier();

        Message r = ZuzuMsgCall(port, MSG_PING, 0, 0);

        barrier();
        uint32_t end = read_pmccntr();
        barrier();

        /* Unsigned subtraction: correct mod-2^32 even if PMCCNTR wrapped
         * between the two reads. */
        g_samples[i] = end - start;

        if ((r.w0 != 0 || r.w1 != 1) && errors == 0) {
            first_err_r0 = r.w0;
            first_err_r1 = r.w1;
        }
        if (r.w0 != 0 || r.w1 != 1) {
            errors++;
        }
    }

    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        uint32_t c = g_samples[i];
        if (c < min) min = c;
        if (c > max) max = c;
        sum += c;
    }
    uint64_t mean_x100 = (sum * 100) / BENCHMARK_ITERATIONS;

    printf("IPC RTT (zuzu_msg_call round trip), %u iterations (+%u warm-up)\n",
           (unsigned)BENCHMARK_ITERATIONS, (unsigned)WARMUP_ITERATIONS);

    if (errors) {
        printf("  %u/%u replies were unexpected (first: w0=%d w1=%u)\n",
               errors, (unsigned)BENCHMARK_ITERATIONS, first_err_r0, first_err_r1);
    }

    printf("  min: %u cycles\n", min);
    printf("  avg: %u.%02u cycles\n", (unsigned)(mean_x100 / 100), (unsigned)(mean_x100 % 100));
    printf("  max: %u cycles  (max includes scheduler/IRQ jitter; min is the best-case cost)\n", max);

    uint32_t cycles_per_us = calibrate_cycles_per_us();
    if (cycles_per_us > 0) {
        uint64_t min_ns = ((uint64_t)min * 1000) / cycles_per_us;
        uint64_t avg_ns = ((uint64_t)(sum / BENCHMARK_ITERATIONS) * 1000) / cycles_per_us;
        printf("  approx (sleep-calibrated @ ~%u cycles/us): min %llu ns, avg %llu ns\n",
               cycles_per_us, (unsigned long long)min_ns, (unsigned long long)avg_ns);
    }
}

static void run_lcall_benchmark(Handle port) {
    uint8_t payload[LCALL_PAYLOAD_LEN];
    memset(payload, 0xA5, sizeof(payload));

    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        LmsgWrite(payload, sizeof(payload));
        ZuzuMsgLcall(port, sizeof(payload));
    }

    uint32_t errors = 0;
    int32_t first_err_r0 = 0;
    uint32_t first_err_r1 = 0;

    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        LmsgWrite(payload, sizeof(payload));

        barrier();
        uint32_t start = read_pmccntr();
        barrier();

        Message r = ZuzuMsgLcall(port, sizeof(payload));

        barrier();
        uint32_t end = read_pmccntr();
        barrier();

        /* Unsigned subtraction: correct mod-2^32 even if PMCCNTR wrapped
         * between the two reads. */
        g_samples[i] = end - start;

        if ((r.w0 != 0 || r.w1 != sizeof(payload)) && errors == 0) {
            first_err_r0 = r.w0;
            first_err_r1 = r.w1;
        }
        if (r.w0 != 0 || r.w1 != sizeof(payload)) {
            errors++;
        }
    }

    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        uint32_t c = g_samples[i];
        if (c < min) min = c;
        if (c > max) max = c;
        sum += c;
    }
    uint64_t mean_x100 = (sum * 100) / BENCHMARK_ITERATIONS;

    printf("IPC LRTT (ZuzuMsgLcall round trip, %u-byte payload), %u iterations (+%u warm-up)\n",
           (unsigned)LCALL_PAYLOAD_LEN, (unsigned)BENCHMARK_ITERATIONS, (unsigned)WARMUP_ITERATIONS);

    if (errors) {
        printf("  %u/%u replies were unexpected (first: w0=%d w1=%u)\n",
               errors, (unsigned)BENCHMARK_ITERATIONS, first_err_r0, first_err_r1);
    }

    printf("  min: %u cycles\n", min);
    printf("  avg: %u.%02u cycles\n", (unsigned)(mean_x100 / 100), (unsigned)(mean_x100 % 100));
    printf("  max: %u cycles  (max includes scheduler/IRQ jitter; min is the best-case cost)\n", max);

    uint32_t cycles_per_us = calibrate_cycles_per_us();
    if (cycles_per_us > 0) {
        uint64_t min_ns = ((uint64_t)min * 1000) / cycles_per_us;
        uint64_t avg_ns = ((uint64_t)(sum / BENCHMARK_ITERATIONS) * 1000) / cycles_per_us;
        printf("  approx (sleep-calibrated @ ~%u cycles/us): min %llu ns, avg %llu ns\n",
               cycles_per_us, (unsigned long long)min_ns, (unsigned long long)avg_ns);
    }
}

static void run_getpid_benchmark() {
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
       (void)ZuzuGetPid();
    }

    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        barrier();
        uint32_t start = read_pmccntr();
        barrier();

        (void)ZuzuGetPid();

        barrier();
        uint32_t end = read_pmccntr();
        barrier();

        /* Unsigned subtraction: correct mod-2^32 even if PMCCNTR wrapped
         * between the two reads. */
        g_samples[i] = end - start;

    }

    uint32_t min = UINT32_MAX, max = 0;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        uint32_t c = g_samples[i];
        if (c < min) min = c;
        if (c > max) max = c;
        sum += c;
    }
    uint64_t mean_x100 = (sum * 100) / BENCHMARK_ITERATIONS;

    printf("getpid, %u iterations (+%u warm-up)\n",
           (unsigned)BENCHMARK_ITERATIONS, (unsigned)WARMUP_ITERATIONS);

    printf("  min: %u cycles\n", min);
    printf("  avg: %u.%02u cycles\n", (unsigned)(mean_x100 / 100), (unsigned)(mean_x100 % 100));
    printf("  max: %u cycles  (max includes scheduler/IRQ jitter; min is the best-case cost)\n", max);

    uint32_t cycles_per_us = calibrate_cycles_per_us();
    if (cycles_per_us > 0) {
        uint64_t min_ns = ((uint64_t)min * 1000) / cycles_per_us;
        uint64_t avg_ns = ((uint64_t)(sum / BENCHMARK_ITERATIONS) * 1000) / cycles_per_us;
        printf("  approx (sleep-calibrated @ ~%u cycles/us): min %llu ns, avg %llu ns\n",
               cycles_per_us, (unsigned long long)min_ns, (unsigned long long)avg_ns);
    }
}

int main(void) {
    printf("zuzuOS general speed suite\n");

    printf("getpid():");

    run_getpid_benchmark();

    Handle port = ZuzuPortCreate();
    if (port < 0) {
        printf("Couldn't get handle: %s\n", StrToError(port));
        return 1;
    }

    uint8_t *stack = malloc(THREAD_STACK_SIZE);
    if (!stack) {
        printf("Couldn't allocate thread stack\n");
        ZuzuDestroy(port);
        return 1;
    }

    /* Descending frame model: stack pointer starts at the top of the block. */
    Tid tid = ZuzuTMake(echo_server_thread, stack + THREAD_STACK_SIZE, &port);
    if (tid < 0) {
        printf("Couldn't make thread: %s\n", StrToError(tid));
        free(stack);
        ZuzuDestroy(port);
        return 1;
    }

    run_benchmark(port);

    ZuzuMsgCall(port, MSG_QUIT, 0, 0);
    ZuzuTJoin(tid);

    free(stack);
    ZuzuDestroy(port);

    Handle lport = ZuzuPortCreate();
    if (lport < 0) {
        printf("Couldn't get lcall handle: %s\n", StrToError(lport));
        return 1;
    }

    uint8_t *lstack = malloc(THREAD_STACK_SIZE);
    if (!lstack) {
        printf("Couldn't allocate lcall thread stack\n");
        ZuzuDestroy(lport);
        return 1;
    }

    Tid ltid = ZuzuTMake(lcall_echo_server_thread, lstack + THREAD_STACK_SIZE, &lport);
    if (ltid < 0) {
        printf("Couldn't make lcall thread: %s\n", StrToError(ltid));
        free(lstack);
        ZuzuDestroy(lport);
        return 1;
    }

    run_lcall_benchmark(lport);

    ZuzuMsgLcall(lport, LCALL_QUIT_LEN);
    ZuzuTJoin(ltid);

    free(lstack);
    ZuzuDestroy(lport);
    return 0;
}
