/**
 * @file
 * @brief Per-thread control block (TCB): identity, priority and message box.
 *
 * Every thread has one TCB slot in its space; the kernel fills the header and
 * IPC copies messages through @c buf. GetTls() finds the current thread's slot.
 */

#ifndef TCB_H
#define TCB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/tls.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#ifndef PAGE_SIZE
/** @brief Page size in bytes. */
#define PAGE_SIZE 4096
#endif

/** @brief Bytes of TCB header before the message box. */
#define TCB_HDR_SIZE 64
/** @brief Size of a thread's message box: the most one Call() or Reply() can carry. */
#define MSG_BUF_SIZE 512
/** @brief Bytes per TCB slot (576). */
#define TCB_SLOT_SIZE (TCB_HDR_SIZE + MSG_BUF_SIZE)
/** @brief TCB slots per page (7). */
#define SLOTS_PER_PAGE (PAGE_SIZE / TCB_SLOT_SIZE)
/** @brief Most threads per space; 0xFF is reserved to mean "no slot". */
#define TCB_MAX_SLOTS 255
/** @brief Pages reserved for TCB slots in every space (37). */
#define MAX_TCB_PAGES ((TCB_MAX_SLOTS + SLOTS_PER_PAGE - 1) / SLOTS_PER_PAGE)

_Static_assert(SLOTS_PER_PAGE *TCB_SLOT_SIZE <= PAGE_SIZE, "slots must fit within one page");
_Static_assert(TCB_MAX_SLOTS <= 256, "bitmap is uint64_t[4] = 256 bits");

/** @brief One thread's TCB slot. The header is written by the kernel; treat it as read-only. */
typedef struct {
    void *msg_buf;                      /**< Address of @c buf, set by the kernel. */
    Tid tid;                            /**< Thread ID. */
    Spid spid;                          /**< ID of the owning space. */
    uint8_t prio;                       /**< Base priority. */
    uint8_t max_prio;                   /**< Highest priority the thread may set. */
    uint8_t padding[TCB_HDR_SIZE - 14]; /**< Pads the header to TCB_HDR_SIZE. */
    uint8_t buf[MSG_BUF_SIZE];          /**< Message box used by Call(), Reply() and receives. */
} ThreadLocalData;

_Static_assert(sizeof(ThreadLocalData) == TCB_SLOT_SIZE, "ThreadData must fill its slot");

/**
 * @brief Returns the calling thread's TCB slot.
 *
 * @return The TCB of the current thread.
 */
static inline ThreadLocalData *GetTls(void) { return (ThreadLocalData *)ArchGetTlsPointer(); }

#ifdef __cplusplus
}
#endif

#endif // TCB_H
