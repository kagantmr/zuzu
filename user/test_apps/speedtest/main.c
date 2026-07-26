#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <zuzu/types.h>
#include <zuzu/task.h>
#include <zuzu/msg.h>

#define BENCHMARK_ITERATIONS 1000
#define WARMUP_ITERATIONS    5
#define THREAD_STACK_SIZE    4096
#define CALIBRATION_MS       500

/* First payload word (w1) the client sends to ask the echo thread to exit.
 * zuzu_msg_recv() of a call hands the receiver: r0 = reply handle,
 * r1 = caller pid, r2/r3 = the caller's w1/w2 - so the sentinel must be
 * checked against r2, not r1. */
#define MSG_QUIT 0xFFFFFFFFu
#define MSG_PING 0u

/* Direct read of the ARMv7 32-bit cycle counter. The PMU is already enabled
 * and opened up for PL0 access at boot (see pmu_init() in arch/arm/early.c),
 * so no setup is needed here. */
static void echo_server_thread(void *arg) {
    handle_t port = *(handle_t *)arg;

    for (;;) {
        msg_t cmd = zuzu_msg_recv(port, TIMEOUT_INFINITE);

        if (cmd.r2 == MSG_QUIT) {
            zuzu_msg_reply(cmd.r0, 1, 0, 0);
            zuzu_tquit(ZUZU_OK);
        }

        zuzu_msg_reply(cmd.r0, 1, 0, 0);
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

    zuzu_sleep(CALIBRATION_MS);

    barrier();
    uint32_t end = read_pmccntr();
    barrier();

    return (end - start) / (CALIBRATION_MS * 1000u);
}

static uint32_t g_samples[BENCHMARK_ITERATIONS];

static void run_benchmark(handle_t port) {
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        zuzu_msg_call(port, MSG_PING, 0, 0);
    }

    uint32_t errors = 0;
    int32_t first_err_r0 = 0;
    uint32_t first_err_r1 = 0;

    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        barrier();
        uint32_t start = read_pmccntr();
        barrier();

        msg_t r = zuzu_msg_call(port, MSG_PING, 0, 0);

        barrier();
        uint32_t end = read_pmccntr();
        barrier();

        /* Unsigned subtraction: correct mod-2^32 even if PMCCNTR wrapped
         * between the two reads. */
        g_samples[i] = end - start;

        if ((r.r0 != 0 || r.r1 != 1) && errors == 0) {
            first_err_r0 = r.r0;
            first_err_r1 = r.r1;
        }
        if (r.r0 != 0 || r.r1 != 1) {
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
        printf("  %u/%u replies were unexpected (first: r0=%d r1=%u)\n",
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
       (void)zuzu_getpid();
    }

    for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
        barrier();
        uint32_t start = read_pmccntr();
        barrier();

        zpid_t pid = zuzu_getpid();

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

    handle_t port = zuzu_port_create();
    if (port < 0) {
        printf("Couldn't get handle: %s\n", strtoerror(port));
        return 1;
    }

    uint8_t *stack = malloc(THREAD_STACK_SIZE);
    if (!stack) {
        printf("Couldn't allocate thread stack\n");
        zuzu_destroy(port);
        return 1;
    }

    /* Descending frame model: stack pointer starts at the top of the block. */
    tid_t tid = zuzu_tmake(echo_server_thread, stack + THREAD_STACK_SIZE, &port);
    if (tid < 0) {
        printf("Couldn't make thread: %s\n", strtoerror(tid));
        free(stack);
        zuzu_destroy(port);
        return 1;
    }

    run_benchmark(port);

    zuzu_msg_call(port, MSG_QUIT, 0, 0);
    zuzu_tjoin(tid);

    free(stack);
    zuzu_destroy(port);
    return 0;
}
