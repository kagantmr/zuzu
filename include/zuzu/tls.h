#ifndef TCB_H
#define TCB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/tls.h>
#include <stddef.h>
#include <stdint.h>
#include <zuzu/types.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define TCB_HDR_SIZE 64
#define LMSG_BUF_SIZE 512
#define TCB_SLOT_SIZE (TCB_HDR_SIZE + LMSG_BUF_SIZE) /* 576 */
#define SLOTS_PER_PAGE (PAGE_SIZE / TCB_SLOT_SIZE)   /* 7 */
#define TCB_MAX_SLOTS 255			     /* 0xFF reserved as TCB_SLOT_NONE */
#define MAX_TCB_PAGES ((TCB_MAX_SLOTS + SLOTS_PER_PAGE - 1) / SLOTS_PER_PAGE) /* ceil = 37 */

_Static_assert(SLOTS_PER_PAGE *TCB_SLOT_SIZE <= PAGE_SIZE, "slots must fit within one page");
_Static_assert(TCB_MAX_SLOTS <= 256, "bitmap is uint64_t[4] = 256 bits");

typedef struct {
	void *LmsgBuf; /* this slot's buf; kernel owns the location */
	Tid tid;
	Pid pid;
	uint8_t _pad[TCB_HDR_SIZE - 12];
	uint8_t buf[LMSG_BUF_SIZE];
} ThreadData;

_Static_assert(sizeof(ThreadData) == TCB_SLOT_SIZE, "ThreadData must fill its slot");

/**
 * TLS accessor for the current thread's TCB.
 */
static inline ThreadData *ZuzuTLS(void) { return (ThreadData *)arch_get_thread_ptr(); }

#ifdef __cplusplus
}
#endif

#endif // TCB_H
