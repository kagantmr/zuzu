#include "globals.h"
#include "zuzu/memprot.h"
#include "zuzu/umem.h"

#include <arch/cycles.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zuzu/bench.h>
#include <zuzu/lmsg.h>
#include <zuzu/msg.h>
#include <zuzu/protocols/nametable.h>
#include <zuzu/task.h>
#include <zuzu/types.h>

/* First payload word (w1) the client sends to ask the echo thread to exit.
 * zuzu_msg_recv() of a call hands the receiver: w0 = reply handle,
 * w1 = caller pid, w2/w3 = the caller's w1/w2 - so the sentinel must be
 * checked against w2, not w1. */
#define MSG_QUIT 0xFFFFFFFFu
#define MSG_PING 0u

/* ZuzuMsgLcall has no w1/w2 word arguments -- the only thing SysMsgRecv
 * hands the receiver for an lcall is the transferred buffer length in w2
 * (see kernel/ipc/sys_ipc.c). LCALL_QUIT_LEN just needs to be distinct from
 * LCALL_PAYLOAD_LEN so the server can tell "shut down" from "echo this". */
#define LCALL_PAYLOAD_LEN 32u
#define LCALL_QUIT_LEN 4u

/* Cross-process IPC RTT: same protocol, same iteration count as the
 * existing thread-based run_benchmark(), but a dedicated 500-sample
 * warm-up rather than the shared (5-iteration) WARMUP_ITERATIONS --
 * see run_cross_process_benchmark()'s own comment for why it needs a
 * longer warm-up than the same-process variant. */
#define XPROC_WARMUP_ITERATIONS 500u

/* Every value this suite reports (min-of-N, so *_x100 carries two decimal
 * digits of fixed-point precision for the average). ok = false means the
 * benchmark couldn't run at all (e.g. the cross-process child never
 * registered) -- its row in the Markdown table prints "n/a" rather than a
 * misleading zero. */
typedef struct {
	uint32_t min;
	uint64_t avg_x100;
	uint32_t max;
	bool ok;
} BenchmarkResult;

/* Direct read of the ARMv7 32-bit cycle counter. The PMU is already enabled
 * and opened up for PL0 access at boot (see pmu_init() in arch/arm/early.c),
 * so no setup is needed here. */
static void echo_server_thread(void *arg)
{
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
static void lcall_echo_server_thread(void *arg)
{
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
static uint32_t calibrate_cycles_per_us(void)
{
	ArchIsb();
	uint32_t start = ArchStartMeasurement();
	ArchIsb();

	ZuzuSleep(CALIBRATION_MS);

	ArchIsb();
	uint32_t end = ArchEndMeasurement();
	ArchIsb();

	return (end - start) / (CALIBRATION_MS * 1000u);
}

static uint32_t g_samples[BENCHMARK_ITERATIONS];

static BenchmarkResult run_benchmark(Handle port)
{
	for (int i = 0; i < WARMUP_ITERATIONS; i++) {
		ZuzuMsgCall(port, MSG_PING, 0, 0);
	}

	uint32_t errors = 0;
	int32_t first_err_r0 = 0;
	uint32_t first_err_r1 = 0;

	for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
		ArchIsb();
		uint32_t start = ArchMeasure();
		ArchIsb();

		Message r = ZuzuMsgCall(port, MSG_PING, 0, 0);

		ArchIsb();
		uint32_t end = ArchMeasure();
		ArchIsb();

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
		if (c < min)
			min = c;
		if (c > max)
			max = c;
		sum += c;
	}
	uint64_t mean_x100 = (sum * 100) / BENCHMARK_ITERATIONS;

	/* "same-process, thread" is the disambiguating half of this label --
	 * see run_cross_process_benchmark() for the counterpart that spawns
	 * sender and receiver as separate processes instead. */
	printf("IPC RTT (same-process, thread; zuzu_msg_call round trip), %u iterations (+%u warm-up)\n",
	       (unsigned)BENCHMARK_ITERATIONS, (unsigned)WARMUP_ITERATIONS);

	if (errors) {
		printf("  %u/%u replies were unexpected (first: w0=%d w1=%u)\n", errors,
		       (unsigned)BENCHMARK_ITERATIONS, first_err_r0, first_err_r1);
	}

	printf("  min: %u cycles\n", min);
	printf("  avg: %u.%02u cycles\n", (unsigned)(mean_x100 / 100), (unsigned)(mean_x100 % 100));
	printf("  max: %u cycles  (max includes scheduler/IRQ jitter; min is the best-case cost)\n",
	       max);

	uint32_t cycles_per_us = calibrate_cycles_per_us();
	if (cycles_per_us > 0) {
		uint64_t min_ns = ((uint64_t)min * 1000) / cycles_per_us;
		uint64_t avg_ns = ((uint64_t)(sum / BENCHMARK_ITERATIONS) * 1000) / cycles_per_us;
		printf("  approx (sleep-calibrated @ ~%u cycles/us): min %llu ns, avg %llu ns\n",
		       cycles_per_us, (unsigned long long)min_ns, (unsigned long long)avg_ns);
	}

	return (BenchmarkResult){ .min = min, .avg_x100 = mean_x100, .max = max, .ok = true };
}

/* speedtest_ipc_child (see that file) registers its port under nametable
 * name "sipc" at boot, well before speedtest itself runs (it isn't
 * :late in initrd/boot.manifest). Still poll rather than assume it has
 * already registered by the time we get here -- same defensive pattern
 * pl011drv's wait_for_devmgr() uses for devmgr. */
static Handle lookup_ipc_child(void)
{
	for (int tries = 0; tries < 200; tries++) {
		Message r = ZuzuMsgCall(NT_PORT, NT_LOOKUP, nt_pack("sipc"), 0);
		if ((int32_t)r.w1 == NT_LU_OK)
			return (Handle)r.w2;
		ZuzuSleep(10);
	}
	return -1;
}

/* Cross-process counterpart to run_benchmark(): identical ping-pong
 * protocol, identical iteration count, against speedtest_ipc_child
 * instead of an in-process thread -- so every extra cycle this reports
 * over the same-process number is the cost of the full context-switch
 * path (TTBR0 write, ASID/TLB handling) that a thread-to-thread handoff
 * never pays, not scheduler-pick-and-register-copy cost.
 *
 * The warm-up is 500 iterations rather than the same-process variant's
 * WARMUP_ITERATIONS (5): the first handful of cross-process round trips
 * also pay for one-time costs a thread-to-thread handoff doesn't have
 * to (the TLB/ASID entries for the child's address space aren't resident
 * until something actually switches into it), and those need to be
 * amortized out of the measured window, not just the branch
 * predictor/icache warm-up the same-process benchmark's 5 iterations
 * cover. */
static BenchmarkResult run_cross_process_benchmark(void)
{
	Handle port = lookup_ipc_child();
	if (port < 0) {
		printf("IPC RTT (cross-process; zuzu_msg_call round trip): "
		       "speedtest_ipc_child never registered, skipping\n");
		return (BenchmarkResult){ .ok = false };
	}

	for (uint32_t i = 0; i < XPROC_WARMUP_ITERATIONS; i++) {
		ZuzuMsgCall(port, MSG_PING, 0, 0);
	}

	uint32_t errors = 0;
	int32_t first_err_r0 = 0;
	uint32_t first_err_r1 = 0;

	for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
		ArchIsb();
		uint32_t start = ArchMeasure();
		ArchIsb();

		Message r = ZuzuMsgCall(port, MSG_PING, 0, 0);

		ArchIsb();
		uint32_t end = ArchMeasure();
		ArchIsb();

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
		if (c < min)
			min = c;
		if (c > max)
			max = c;
		sum += c;
	}
	uint64_t mean_x100 = (sum * 100) / BENCHMARK_ITERATIONS;

	printf("IPC RTT (cross-process; zuzu_msg_call round trip), %u iterations (+%u warm-up)\n",
	       (unsigned)BENCHMARK_ITERATIONS, (unsigned)XPROC_WARMUP_ITERATIONS);

	if (errors) {
		printf("  %u/%u replies were unexpected (first: w0=%d w1=%u)\n", errors,
		       (unsigned)BENCHMARK_ITERATIONS, first_err_r0, first_err_r1);
	}

	printf("  min: %u cycles\n", min);
	printf("  avg: %u.%02u cycles\n", (unsigned)(mean_x100 / 100), (unsigned)(mean_x100 % 100));
	printf("  max: %u cycles  (max includes scheduler/IRQ jitter; min is the best-case cost)\n",
	       max);

	uint32_t cycles_per_us = calibrate_cycles_per_us();
	if (cycles_per_us > 0) {
		uint64_t min_ns = ((uint64_t)min * 1000) / cycles_per_us;
		uint64_t avg_ns = ((uint64_t)(sum / BENCHMARK_ITERATIONS) * 1000) / cycles_per_us;
		printf("  approx (sleep-calibrated @ ~%u cycles/us): min %llu ns, avg %llu ns\n",
		       cycles_per_us, (unsigned long long)min_ns, (unsigned long long)avg_ns);
	}

	return (BenchmarkResult){ .min = min, .avg_x100 = mean_x100, .max = max, .ok = true };
}

static BenchmarkResult run_lcall_benchmark(Handle port)
{
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

		ArchIsb();
		uint32_t start = ArchMeasure();
		ArchIsb();

		Message r = ZuzuMsgLcall(port, sizeof(payload));

		ArchIsb();
		uint32_t end = ArchMeasure();
		ArchIsb();

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
		if (c < min)
			min = c;
		if (c > max)
			max = c;
		sum += c;
	}
	uint64_t mean_x100 = (sum * 100) / BENCHMARK_ITERATIONS;

	printf("IPC LRTT (ZuzuMsgLcall round trip, %u-byte payload), %u iterations (+%u warm-up)\n",
	       (unsigned)LCALL_PAYLOAD_LEN, (unsigned)BENCHMARK_ITERATIONS,
	       (unsigned)WARMUP_ITERATIONS);

	if (errors) {
		printf("  %u/%u replies were unexpected (first: w0=%d w1=%u)\n", errors,
		       (unsigned)BENCHMARK_ITERATIONS, first_err_r0, first_err_r1);
	}

	printf("  min: %u cycles\n", min);
	printf("  avg: %u.%02u cycles\n", (unsigned)(mean_x100 / 100), (unsigned)(mean_x100 % 100));
	printf("  max: %u cycles  (max includes scheduler/IRQ jitter; min is the best-case cost)\n",
	       max);

	uint32_t cycles_per_us = calibrate_cycles_per_us();
	if (cycles_per_us > 0) {
		uint64_t min_ns = ((uint64_t)min * 1000) / cycles_per_us;
		uint64_t avg_ns = ((uint64_t)(sum / BENCHMARK_ITERATIONS) * 1000) / cycles_per_us;
		printf("  approx (sleep-calibrated @ ~%u cycles/us): min %llu ns, avg %llu ns\n",
		       cycles_per_us, (unsigned long long)min_ns, (unsigned long long)avg_ns);
	}

	return (BenchmarkResult){ .min = min, .avg_x100 = mean_x100, .max = max, .ok = true };
}

static BenchmarkResult run_getpid_benchmark(void)
{
	for (int i = 0; i < WARMUP_ITERATIONS; i++) {
		(void)ZuzuGetPid();
	}

	for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
		ArchIsb();
		uint32_t start = ArchMeasure();
		ArchIsb();

		(void)ZuzuGetPid();

		ArchIsb();
		uint32_t end = ArchMeasure();
		ArchIsb();

		/* Unsigned subtraction: correct mod-2^32 even if PMCCNTR wrapped
		 * between the two reads. */
		g_samples[i] = end - start;
	}

	uint32_t min = UINT32_MAX, max = 0;
	uint64_t sum = 0;
	for (uint32_t i = 0; i < BENCHMARK_ITERATIONS; i++) {
		uint32_t c = g_samples[i];
		if (c < min)
			min = c;
		if (c > max)
			max = c;
		sum += c;
	}
	uint64_t mean_x100 = (sum * 100) / BENCHMARK_ITERATIONS;

	printf("getpid, %u iterations (+%u warm-up)\n", (unsigned)BENCHMARK_ITERATIONS,
	       (unsigned)WARMUP_ITERATIONS);

	printf("  min: %u cycles\n", min);
	printf("  avg: %u.%02u cycles\n", (unsigned)(mean_x100 / 100), (unsigned)(mean_x100 % 100));
	printf("  max: %u cycles  (max includes scheduler/IRQ jitter; min is the best-case cost)\n",
	       max);

	uint32_t cycles_per_us = calibrate_cycles_per_us();
	if (cycles_per_us > 0) {
		uint64_t min_ns = ((uint64_t)min * 1000) / cycles_per_us;
		uint64_t avg_ns = ((uint64_t)(sum / BENCHMARK_ITERATIONS) * 1000) / cycles_per_us;
		printf("  approx (sleep-calibrated @ ~%u cycles/us): min %llu ns, avg %llu ns\n",
		       cycles_per_us, (unsigned long long)min_ns, (unsigned long long)avg_ns);
	}

	return (BenchmarkResult){ .min = min, .avg_x100 = mean_x100, .max = max, .ok = true };
}

/* Prints one copy-pasteable Markdown table row covering every column this
 * binary itself measures (min-of-N cycles, matching the "Control (1.1)
 * none" baseline row's convention) -- paste straight into BENCHMARKS.md,
 * fill in the leading label cell for whatever config this run was built
 * with. Kernel-side ZUZU_BENCH counters print their own [BENCH] lines
 * separately on the kernel console; they aren't part of this row since
 * this process has no way to read them back. */
static void print_markdown_row(BenchmarkResult getpid, BenchmarkResult ipc_thread,
			       BenchmarkResult ipc_xproc, BenchmarkResult lmsg)
{
	char getpid_s[16], thread_s[16], xproc_s[16], lmsg_s[16];

	if (getpid.ok)
		snprintf(getpid_s, sizeof(getpid_s), "%u", getpid.min);
	else
		snprintf(getpid_s, sizeof(getpid_s), "n/a");

	if (ipc_thread.ok)
		snprintf(thread_s, sizeof(thread_s), "%u", ipc_thread.min);
	else
		snprintf(thread_s, sizeof(thread_s), "n/a");

	if (ipc_xproc.ok)
		snprintf(xproc_s, sizeof(xproc_s), "%u", ipc_xproc.min);
	else
		snprintf(xproc_s, sizeof(xproc_s), "n/a");

	if (lmsg.ok)
		snprintf(lmsg_s, sizeof(lmsg_s), "%u", lmsg.min);
	else
		snprintf(lmsg_s, sizeof(lmsg_s), "n/a");

	printf("\nMarkdown row for BENCHMARKS.md (min cycles; fill in the label):\n");
	printf("| <label> | %s | %s | %s | %s |\n", getpid_s, thread_s, xproc_s, lmsg_s);
	printf("(columns: getpid | IPC RTT same-process,thread | "
	       "IPC RTT cross-process | Lmsg RTT)\n");
}

#ifdef ZUZU_BENCH

static void bench_print(const char *label, const BenchResult *r)
{
	uint64_t avg_x100 = r->count ? (r->sum * 100) / r->count : 0;
	printf("[BENCH] %-32s min=%-8u avg=%u.%02u max=%-8u (cycles, n=%u)\n", label, r->min,
	       (uint32_t)(avg_x100 / 100), (uint32_t)(avg_x100 % 100), r->max, r->count);
}

/* User-side SVC entry/exit: PMCCNTR read immediately before a trivial
 * syscall (getpid) and immediately after it returns. Running min/max/sum
 * only -- 100000 samples is too big to keep as raw data. */
static void run_svc_entry_exit_benchmark(void)
{
	for (uint32_t i = 0; i < ZUZU_BENCH_WARMUP_ITERS; i++) {
		(void)ZuzuGetPid();
	}

	BenchResult r = { 0 };
	for (uint32_t i = 0; i < ZUZU_BENCH_ITERS; i++) {
		uint32_t start = ArchMeasure();
		(void)ZuzuGetPid();
		uint32_t end = ArchMeasure();
		bench_result_record(&r, end - start);
	}

	bench_print("SVC entry/exit (getpid)", &r);
}

/* Drives ZuzuMsgCall/ZuzuMsgRecv enough times for the kernel-side
 * ZUZU_BENCH counters bracketing the handle-table lookup, the direct-switch
 * IPC handoff, and reply-cap alloc/free (see kernel/ipc/sys_msg.c and
 * kernel/mm/alloc.c) to clear their warm-up and self-report on the kernel
 * console. This process only needs to generate the traffic -- the
 * measurement and its printout live entirely in the kernel. */
static void run_kernel_ipc_bench_driver(Handle port)
{
	for (uint32_t i = 0; i < ZUZU_BENCH_WARMUP_ITERS + ZUZU_BENCH_ITERS; i++) {
		ZuzuMsgCall(port, MSG_PING, 0, 0);
	}
}

/* Drives ZuzuMemMap/write/ZuzuMemUnmap enough times for the kernel-side
 * SysMemMap and lazy-mapping-translation-fault counters to self-report. */
static void run_kernel_memmap_bench_driver(void)
{
	for (uint32_t i = 0; i < ZUZU_BENCH_WARMUP_ITERS + ZUZU_BENCH_ITERS; i++) {
		uint32_t *region = ZuzuMemMap(HANDLE_ANON, 4096, PROT_RW, 0);
		*region = 0xCAFEBABE;
		ZuzuMemUnmap(region);
	}
}

#endif /* ZUZU_BENCH */

int main(void)
{
	printf("zuzuOS general speed suite\n");

	printf("getpid():");

	BenchmarkResult r_getpid = run_getpid_benchmark();

#ifdef ZUZU_BENCH
	run_svc_entry_exit_benchmark();
#endif

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

	BenchmarkResult r_ipc_thread = run_benchmark(port);

#ifdef ZUZU_BENCH
	run_kernel_ipc_bench_driver(port);
#endif

	ZuzuMsgCall(port, MSG_QUIT, 0, 0);
	ZuzuTJoin(tid);

	free(stack);
	ZuzuDestroy(port);

	/* speedtest_ipc_child is a permanent boot service (see that file's
	 * header comment), not something this process spawns/tears down --
	 * nothing to clean up here beyond the benchmark itself. */
	BenchmarkResult r_ipc_xproc = run_cross_process_benchmark();

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

	BenchmarkResult r_lmsg = run_lcall_benchmark(lport);

	ZuzuMsgLcall(lport, LCALL_QUIT_LEN);
	ZuzuTJoin(ltid);

	free(lstack);
	ZuzuDestroy(lport);

#ifdef ZUZU_BENCH
	/* Drives SysMemMap and the lazy-mapping translation-fault path; both
	 * self-report on the kernel console once their warm-up clears. */
	run_kernel_memmap_bench_driver();
#endif

	print_markdown_row(r_getpid, r_ipc_thread, r_ipc_xproc, r_lmsg);

	return 0;
}
