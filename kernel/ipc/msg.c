#include "msg.h"
#include <string.h>
#include <zuzu/tls.h>
#include "kernel/mm/vmm.h"

#ifdef CONFIG_ZUZU_BENCH

#include "kernel/bench.h"

BENCH_STAT(g_bench_ipc_buf_copy_memcpy, "ipc_buf_copy: memcpy");
BENCH_STAT(g_bench_ipc_buf_copy_wordcopy, "ipc_buf_copy: hand-rolled word-copy");
static uint8_t g_bench_wordcopy_scratch[MSG_BUF_SIZE] __attribute__((aligned(4)));
#endif

void __hot MsgBufCopy(TaskObject *restrict src, TaskObject *restrict dst, size_t len)
{
	if (!len || !src->msg_buf_phys_addr || !dst->msg_buf_phys_addr)
		return;
	if (len > MSG_BUF_SIZE)
		return;

	const void *srcp = (const void *)PA_TO_VA(src->msg_buf_phys_addr);
	void *dstp = (void *)PA_TO_VA(dst->msg_buf_phys_addr);

#ifdef CONFIG_ZUZU_BENCH
	uint32_t bench_start = BENCH_BEGIN();
#endif
	memcpy(dstp, srcp, len);
#ifdef CONFIG_ZUZU_BENCH
	BENCH_END(g_bench_ipc_buf_copy_memcpy, bench_start);

	if (((uintptr_t)srcp & 3u) == 0 && (len & 3u) == 0) {
		bench_start = BENCH_BEGIN();
		const uint32_t *ws = (const uint32_t *)srcp;
		uint32_t *wd = (uint32_t *)(void *)g_bench_wordcopy_scratch;
		uint32_t nwords = len / 4u;
		for (uint32_t i = 0; i < nwords; i++)
			wd[i] = ws[i];
		BENCH_END(g_bench_ipc_buf_copy_wordcopy, bench_start);
	}
#endif
}
