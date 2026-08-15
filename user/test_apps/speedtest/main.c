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
#include <zuzu/cap.h>
#include <zuzu/channel.h>
#include <zuzu/lmsg.h>
#include <zuzu/msg.h>
#include <zuzu/protocols/exec.h>
#include <zuzu/service.h>
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
 * LCALL_PAYLOAD_LEN and from every size in the ZUZU_BENCH payload-size
 * sweep (0/4/8/16/32/64/128/256, see run_lcall_sweep_benchmark) so the
 * server can tell "shut down" from "echo this". */
#define LCALL_PAYLOAD_LEN 32u
#define LCALL_QUIT_LEN 500u

/* Cross-process IPC RTT: same protocol, same iteration count as the
 * existing thread-based run_benchmark(), but a dedicated 500-sample
 * warm-up rather than the shared (5-iteration) WARMUP_ITERATIONS --
 * see run_cross_process_benchmark()'s own comment for why it needs a
 * longer warm-up than the same-process variant. */
#define XPROC_WARMUP_ITERATIONS 500u

/* speedtest_ipc_child lives on the SD card next to speedtest itself (see
 * that file) and is spawned on demand -- there's no boot-time instance to
 * find, so speedtest has to bring its own copy up for every run. */
#define CHILD_PATH "/bin/speedtest_ipc_child"
#define CHILD_NAME "speedtest_ipc_child"

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

/* sysd rendezvous + spawn plumbing for speedtest_ipc_child, mirroring
 * zztest's child_spawn (user/test_apps/zztest/main.c) via the same
 * pspawn -> grant -> SYSD_EXEC -> kickstart path zzsh uses -- minus the
 * mode-string argv this suite doesn't need. */
static Handle g_sysd_port = -1;
static Pid g_sysd_pid;

static int sysd_setup(void)
{
	Pid pid;
	Handle h = LookupServiceWithPid("/svc/sysd", &pid);
	if (h < 0)
		return -1;
	g_sysd_port = h;
	g_sysd_pid = pid;
	return 0;
}

typedef struct {
	Handle task;
	Pid pid;
} ChildProc;

/* Spawns speedtest_ipc_child with `port` granted into its handle table
 * pre-kickstart; the child-side slot is passed as its sole argv[1]. That
 * hands the child a live, already-shared port before it ever runs, so
 * there's no rendezvous step on the far side (no nametable lookup, no
 * polling) -- the first ZuzuMsgRecv in the child and the first
 * ZuzuMsgCall in run_cross_process_benchmark() just meet on the port.
 * Returns 0, or a negative err. */
static int32_t spawn_ipc_child(Handle port, ChildProc *out)
{
	TSpawnResult ts = ZuzuPSpawn(CHILD_NAME);
	if (ts.taskHandle < 0)
		return ts.taskHandle;

	int32_t child_slot = ZuzuGrant(port, ts.pid, 0);
	if (child_slot < 0) {
		ZuzuPKill(ts.taskHandle);
		return child_slot;
	}

	int32_t sysd_task = ZuzuGrant(ts.taskHandle, g_sysd_pid, 0);
	if (sysd_task < 0) {
		ZuzuPKill(ts.taskHandle);
		return sysd_task;
	}

	char slot_arg[16];
	snprintf(slot_arg, sizeof(slot_arg), "%d", (int)child_slot);

	/* argbuf = "speedtest_ipc_child\0<slot>\0" */
	char argbuf[sizeof(CHILD_NAME) + sizeof(slot_arg)];
	size_t argpos = 0;
	memcpy(argbuf + argpos, CHILD_NAME, strlen(CHILD_NAME) + 1);
	argpos += strlen(CHILD_NAME) + 1;
	memcpy(argbuf + argpos, slot_arg, strlen(slot_arg) + 1);
	argpos += strlen(slot_arg) + 1;

	/* request = header + "path\0" + argbuf */
	size_t path_len = strlen(CHILD_PATH);
	uint8_t req[sizeof(ExecRequestHeader) + sizeof(CHILD_PATH) + sizeof(argbuf)];
	ExecRequestHeader *hdr = (ExecRequestHeader *)req;
	hdr->cmd = SYSD_EXEC;
	hdr->_pad = 0;
	hdr->taskHandle = (uint16_t)sysd_task;
	hdr->path_len = (uint16_t)path_len;
	hdr->argc = 2;
	hdr->pid = ts.pid;
	memcpy(req + sizeof(*hdr), CHILD_PATH, path_len + 1);
	memcpy(req + sizeof(*hdr) + path_len + 1, argbuf, argpos);

	ExecReply reply;
	int32_t rc = ChannelCall(g_sysd_port, req,
				 (uint32_t)(sizeof(*hdr) + path_len + 1 + argpos), &reply,
				 sizeof(reply));
	if (rc < 0) {
		ZuzuPKill(ts.taskHandle);
		return rc;
	}
	if (rc != (int32_t)sizeof(ExecReply)) {
		ZuzuPKill(ts.taskHandle);
		return ERR_MALFORMED;
	}

	rc = ZuzuKickstart(ts.taskHandle, reply.entry, reply.sp, 2, reply.argv_va);
	if (rc != 0) {
		ZuzuPKill(ts.taskHandle);
		return rc;
	}

	out->task = ts.taskHandle; /* consumed by kickstart (slot freed) */
	out->pid = ts.pid;
	return 0;
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
	if (sysd_setup() != 0) {
		printf("IPC RTT (cross-process; zuzu_msg_call round trip): "
		       "sysd (\"sys\") not registered, skipping\n");
		return (BenchmarkResult){ .ok = false };
	}

	Handle port = ZuzuPortCreate();
	if (port < 0) {
		printf("IPC RTT (cross-process; zuzu_msg_call round trip): "
		       "couldn't create echo port, skipping\n");
		return (BenchmarkResult){ .ok = false };
	}

	ChildProc child = { 0 };
	int32_t spawn_rc = spawn_ipc_child(port, &child);
	if (spawn_rc < 0) {
		printf("IPC RTT (cross-process; zuzu_msg_call round trip): "
		       "speedtest_ipc_child failed to spawn (%s), skipping\n",
		       StrToError(spawn_rc));
		ZuzuDestroy(port);
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

	ZuzuMsgCall(port, MSG_QUIT, 0, 0);
	int32_t exit_status;
	ZuzuWait(child.pid, &exit_status, 0);
	ZuzuDestroy(port);

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

/* Payload-size sweep on Lmsg RTT: same ping-pong protocol as
 * run_lcall_benchmark(), swept across a fixed set of sizes so the slope
 * against payload size can be cross-referenced with the memcpy-only cost
 * (see ipc_buf_copy() in kernel/ipc/sys_msg.c) -- whatever doesn't scale
 * with size is the walk/dispatch flat tax; whatever does is the copy. */
#define LMSG_SWEEP_ITERS 20000u
#define LMSG_SWEEP_WARMUP_ITERS 500u

static void run_lcall_sweep_benchmark(Handle port)
{
	static const uint32_t sizes[] = { 0, 4, 8, 16, 32, 64, 128, 256 };
	static uint8_t payload[256];
	memset(payload, 0xA5, sizeof(payload));

	printf("Lmsg RTT payload-size sweep (zuzu_msg_lcall round trip), %u iterations per size (+%u warm-up)\n",
	       (unsigned)LMSG_SWEEP_ITERS, (unsigned)LMSG_SWEEP_WARMUP_ITERS);

	for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		uint32_t len = sizes[s];

		for (uint32_t i = 0; i < LMSG_SWEEP_WARMUP_ITERS; i++) {
			if (len)
				LmsgWrite(payload, len);
			ZuzuMsgLcall(port, len);
		}

		BenchResult r = { 0 };
		for (uint32_t i = 0; i < LMSG_SWEEP_ITERS; i++) {
			if (len)
				LmsgWrite(payload, len);

			ArchIsb();
			uint32_t start = ArchMeasure();
			ArchIsb();

			ZuzuMsgLcall(port, len);

			ArchIsb();
			uint32_t end = ArchMeasure();
			ArchIsb();

			bench_result_record(&r, end - start);
		}

		char label[32];
		snprintf(label, sizeof(label), "Lmsg RTT (%uB payload)", (unsigned)len);
		bench_print(label, &r);
	}
}

/* Drives ZuzuMemMap/write/ZuzuMemUnmap enough times for the kernel-side
 * SysMemMap and lazy-mapping-translation-fault counters to self-report.
 * 2 pages (not 1): SysMemMap's own standalone VmmCheckUserFault microbench
 * (see kernel/mm/sys_mm.c) needs a 4KB-and-then-some region to run its
 * 2-page-spanning check against. */
static void run_kernel_memmap_bench_driver(void)
{
	for (uint32_t i = 0; i < ZUZU_BENCH_WARMUP_ITERS + ZUZU_BENCH_ITERS; i++) {
		uint32_t *region = ZuzuMemMap(HANDLE_ANON, 2 * 4096, PROT_RW, 0);
		*region = 0xCAFEBABE;
		ZuzuMemUnmap(region);
	}
}

/* Mirrors echo_server_thread, but receives via ZuzuWaitany instead of
 * ZuzuMsgRecv -- for a WAITANY_KIND_CALL match, the reply handle comes back
 * as result.source and the caller's w1 payload as result.w2 (see
 * WaitanyResult in zuzu/types.h and waitany_deliver_sender() in
 * kernel/ipc/sys_msg.c), so the quit sentinel has to be checked there
 * instead of cmd.w2. */
static void waitany_echo_server_thread(void *arg)
{
	Handle port = *(Handle *)arg;

	for (;;) {
		Handle handles[1] = { port };
		WaitanyResult res;
		Err err = ZuzuWaitany(handles, 1, TIMEOUT_INFINITE, &res);
		if (err != 0 || res.kind != WAITANY_KIND_CALL)
			continue;

		if (res.w2 == MSG_QUIT) {
			ZuzuMsgReply((Handle)res.source, 1, 0, 0);
			ZuzuTQuit(ZUZU_OK);
		}
		ZuzuMsgReply((Handle)res.source, 1, 0, 0);
	}
}

/* WaitAny RTT baseline: identical protocol and min-of-N discipline to
 * run_benchmark()'s IPC RTT, except the receiver blocks in ZuzuWaitany on a
 * single handle instead of ZuzuMsgRecv -- msg/lmsg/handle-lookup/direct-
 * handoff/reply-cap all have their own line item already; this is WaitAny's,
 * measured the same "client times its own round trip" way. */
static void run_waitany_benchmark(Handle port)
{
	for (uint32_t i = 0; i < ZUZU_BENCH_WARMUP_ITERS; i++) {
		ZuzuMsgCall(port, MSG_PING, 0, 0);
	}

	BenchResult r = { 0 };
	for (uint32_t i = 0; i < ZUZU_BENCH_ITERS; i++) {
		ArchIsb();
		uint32_t start = ArchMeasure();
		ArchIsb();

		ZuzuMsgCall(port, MSG_PING, 0, 0);

		ArchIsb();
		uint32_t end = ArchMeasure();
		ArchIsb();

		bench_result_record(&r, end - start);
	}

	bench_print("WaitAny RTT (single waiter, zuzu_msg_call round trip)", &r);
}

/* Kernel caps a single WaitAny call at WAITANY_MAX_HANDLES (16, see
 * kernel/ipc/sys_msg.c) -- matched here as the sweep's largest fan-out and
 * as the fixed size of the handle array below. */
#define WAITANY_FANOUT_MAX 16u

typedef struct {
	Handle handles[WAITANY_FANOUT_MAX];
	uint32_t count;
} WaitanyFanoutArg;

/* Waits on every handle in arg->handles -- only the last one (handles[count-1],
 * see run_waitany_fanout_benchmark) ever receives traffic, so
 * waitany_try_once's validate+scan loops always walk the full array before
 * finding a match: the worst case for whatever the fan-out cost curve turns
 * out to be. */
static void waitany_fanout_server_thread(void *arg_)
{
	WaitanyFanoutArg *arg = (WaitanyFanoutArg *)arg_;

	for (;;) {
		WaitanyResult res;
		Err err = ZuzuWaitany(arg->handles, arg->count, TIMEOUT_INFINITE, &res);
		if (err != 0 || res.kind != WAITANY_KIND_CALL)
			continue;

		if (res.w2 == MSG_QUIT) {
			ZuzuMsgReply((Handle)res.source, 1, 0, 0);
			ZuzuTQuit(ZUZU_OK);
		}
		ZuzuMsgReply((Handle)res.source, 1, 0, 0);
	}
}

/* WaitAny fan-out cost: RTT as a function of how many handles a single
 * WaitAny call is waiting on. If waitany_try_once's per-handle validation
 * (see the kernel-side bracket in kernel/ipc/sys_msg.c) is a plain linear
 * scan, this curve should be roughly linear in count; anything worse shows
 * up here too. */
static void run_waitany_fanout_benchmark(void)
{
	static const uint32_t counts[] = { 1, 4, 16 };

	for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
		uint32_t n = counts[c];
		WaitanyFanoutArg arg = { .count = n };
		bool ok = true;

		for (uint32_t i = 0; i < n; i++) {
			arg.handles[i] = ZuzuPortCreate();
			if (arg.handles[i] < 0) {
				ok = false;
				break;
			}
		}
		if (!ok) {
			printf("WaitAny fan-out (%u handles): couldn't create ports, skipping\n",
			       (unsigned)n);
			for (uint32_t i = 0; i < n; i++) {
				if (arg.handles[i] >= 0)
					ZuzuDestroy(arg.handles[i]);
			}
			continue;
		}
		Handle live_port = arg.handles[n - 1];

		uint8_t *stack = malloc(THREAD_STACK_SIZE);
		if (!stack) {
			printf("WaitAny fan-out (%u handles): couldn't allocate thread stack, skipping\n",
			       (unsigned)n);
			for (uint32_t i = 0; i < n; i++)
				ZuzuDestroy(arg.handles[i]);
			continue;
		}

		Tid tid = ZuzuTMake(waitany_fanout_server_thread, stack + THREAD_STACK_SIZE, &arg);
		if (tid < 0) {
			printf("WaitAny fan-out (%u handles): couldn't make thread, skipping\n",
			       (unsigned)n);
			free(stack);
			for (uint32_t i = 0; i < n; i++)
				ZuzuDestroy(arg.handles[i]);
			continue;
		}

		for (uint32_t i = 0; i < ZUZU_BENCH_WARMUP_ITERS; i++) {
			ZuzuMsgCall(live_port, MSG_PING, 0, 0);
		}

		BenchResult r = { 0 };
		for (uint32_t i = 0; i < ZUZU_BENCH_ITERS; i++) {
			ArchIsb();
			uint32_t start = ArchMeasure();
			ArchIsb();

			ZuzuMsgCall(live_port, MSG_PING, 0, 0);

			ArchIsb();
			uint32_t end = ArchMeasure();
			ArchIsb();

			bench_result_record(&r, end - start);
		}

		char label[48];
		snprintf(label, sizeof(label), "WaitAny RTT (fan-out, %u handles)", (unsigned)n);
		bench_print(label, &r);

		ZuzuMsgCall(live_port, MSG_QUIT, 0, 0);
		ZuzuTJoin(tid);
		free(stack);
		for (uint32_t i = 0; i < n; i++)
			ZuzuDestroy(arg.handles[i]);
	}
}

#define WAITANY_SENDER_MAX 16u

typedef struct {
	Handle port;
	uint32_t tag;
} WaitanySenderArg;

static void waitany_sender_thread(void *arg_)
{
	WaitanySenderArg *arg = (WaitanySenderArg *)arg_;
	ZuzuMsgCall(arg->port, arg->tag, 0, 0);
}

/* Not a timing bench: correctness-adjacent check for whether WaitAny's
 * wake order under N pending senders on the same port stays FIFO as N
 * grows. All N sender threads are spawned first and given a short sleep to
 * pile up in the port's sender_queue before draining starts, so this is a
 * best-effort ordering check under whatever this scheduler actually does,
 * not a hard real-time guarantee -- the printed sequence is the ground
 * truth, the PASS/FAIL line just flags whether it stayed monotonic. */
static void run_waitany_wakeorder_check(void)
{
	static const uint32_t counts[] = { 1, 4, 16 };

	for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
		uint32_t n = counts[c];

		Handle port = ZuzuPortCreate();
		if (port < 0) {
			printf("WaitAny wake-order (%u senders): couldn't create port, skipping\n",
			       (unsigned)n);
			continue;
		}

		uint8_t *stacks = malloc((size_t)n * THREAD_STACK_SIZE);
		if (!stacks) {
			printf("WaitAny wake-order (%u senders): couldn't allocate stacks, skipping\n",
			       (unsigned)n);
			ZuzuDestroy(port);
			continue;
		}

		static WaitanySenderArg args[WAITANY_SENDER_MAX];
		Tid tids[WAITANY_SENDER_MAX];

		for (uint32_t i = 0; i < n; i++) {
			args[i].port = port;
			args[i].tag = i;
			tids[i] = ZuzuTMake(waitany_sender_thread, stacks + (i + 1) * THREAD_STACK_SIZE,
					     &args[i]);
		}

		ZuzuSleep(10);

		printf("WaitAny wake order (%u senders): ", (unsigned)n);
		bool fifo_ok = true;
		uint32_t prev = 0;
		for (uint32_t i = 0; i < n; i++) {
			Handle handles[1] = { port };
			WaitanyResult res;
			Err err = ZuzuWaitany(handles, 1, TIMEOUT_INFINITE, &res);
			if (err != 0 || res.kind != WAITANY_KIND_CALL) {
				printf("(waitany error %d) ", err);
				fifo_ok = false;
				break;
			}
			printf("%u ", (unsigned)res.w2);
			if (i > 0 && res.w2 < prev)
				fifo_ok = false;
			prev = res.w2;
			ZuzuMsgReply((Handle)res.source, 1, 0, 0);
		}
		printf("-- %s\n", fifo_ok ? "FIFO order held" : "order NOT monotonic");

		for (uint32_t i = 0; i < n; i++)
			ZuzuTJoin(tids[i]);
		free(stacks);
		ZuzuDestroy(port);
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

	/* run_cross_process_benchmark() owns speedtest_ipc_child's full
	 * lifecycle -- spawn, benchmark, MSG_QUIT, ZuzuWait -- nothing to
	 * clean up here. */
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

#ifdef ZUZU_BENCH
	run_lcall_sweep_benchmark(lport);
#endif

	ZuzuMsgLcall(lport, LCALL_QUIT_LEN);
	ZuzuTJoin(ltid);

	free(lstack);
	ZuzuDestroy(lport);

#ifdef ZUZU_BENCH
	{
		Handle waport = ZuzuPortCreate();
		if (waport < 0) {
			printf("Couldn't get waitany handle: %s\n", StrToError(waport));
		} else {
			uint8_t *wastack = malloc(THREAD_STACK_SIZE);
			if (!wastack) {
				printf("Couldn't allocate waitany thread stack\n");
				ZuzuDestroy(waport);
			} else {
				Tid watid = ZuzuTMake(waitany_echo_server_thread,
						      wastack + THREAD_STACK_SIZE, &waport);
				if (watid < 0) {
					printf("Couldn't make waitany thread: %s\n",
					       StrToError(watid));
					free(wastack);
					ZuzuDestroy(waport);
				} else {
					run_waitany_benchmark(waport);

					ZuzuMsgCall(waport, MSG_QUIT, 0, 0);
					ZuzuTJoin(watid);
					free(wastack);
					ZuzuDestroy(waport);
				}
			}
		}

		run_waitany_fanout_benchmark();
		run_waitany_wakeorder_check();
	}
#endif

#ifdef ZUZU_BENCH
	/* Drives SysMemMap and the lazy-mapping translation-fault path; both
	 * self-report on the kernel console once their warm-up clears. */
	run_kernel_memmap_bench_driver();
#endif

	print_markdown_row(r_getpid, r_ipc_thread, r_ipc_xproc, r_lmsg);

	return 0;
}
