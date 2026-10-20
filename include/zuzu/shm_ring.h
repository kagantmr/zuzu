/**
 * @file shm_ring.h
 * @brief Single-producer, single-consumer byte rings that live in shared memory.
 */

#ifndef ZUZU_SHM_RING_H
#define ZUZU_SHM_RING_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

/**
 * @brief Header of a shared-memory ring. The data bytes live next to it, not inside it.
 *
 * @c head and @c tail are free-running counters that only wrap at 2^32, so the whole of
 * @c size is usable and a full ring is distinguishable from an empty one. The producer owns
 * @c head and the consumer owns @c tail.
 */
typedef struct {
    uint32_t size; /**< Size of the data area in bytes. Must be a power of two. */
    volatile uint32_t head; /**< Total bytes ever produced. Written by the producer. */
    volatile uint32_t tail; /**< Total bytes ever consumed. Written by the consumer. */
} ShmRingHdr;

/**
 * @brief Initializes a ring header to empty.
 *
 * @param[in] r The header to initialize.
 * @param[in] size The size of the data area in bytes; must be a power of two.
 */
void ShmRingInit(ShmRingHdr *r, uint32_t size);

/**
 * @brief Read the producer's counter, ordered before any later read of the data.
 *
 * @param[in] r The ring header.
 * @return The total bytes produced so far.
 */
uint32_t ShmRingLoadHead(const ShmRingHdr *r);
/**
 * @brief Read the consumer's counter, ordered before any later write to the data.
 *
 * @param[in] r The ring header.
 * @return The total bytes consumed so far.
 */
uint32_t ShmRingLoadTail(const ShmRingHdr *r);
/**
 * @brief Make all data written so far visible, then advance the producer's counter.
 *
 * @param[in] r The ring header.
 * @param[in] head The new total of bytes produced.
 */
void ShmRingPublishHead(ShmRingHdr *r, uint32_t head);
/**
 * @brief Finish all reads of the data, then advance the consumer's counter.
 *
 * @param[in] r The ring header.
 * @param[in] tail The new total of bytes consumed.
 */
void ShmRingPublishTail(ShmRingHdr *r, uint32_t tail);

/**
 * @brief Get the number of bytes waiting to be consumed.
 *
 * @param[in] r The ring header.
 * @return The byte count, clamped to the ring size if the counters are corrupt.
 */
uint32_t ShmRingAvail(const ShmRingHdr *r);
/**
 * @brief Get the number of bytes the producer can still write.
 *
 * @param[in] r The ring header.
 * @return The free space, or 0 if the counters are corrupt.
 */
uint32_t ShmRingFree(const ShmRingHdr *r);

/**
 * @brief Copy @p n bytes into the ring at logical position @p pos, wrapping at the end.
 *
 * Does not check for space and does not move any counter, so the caller keeps its own cursor
 * and publishes it with ShmRingPublishHead(). @p size is passed in so a peer cannot change it
 * through shared memory.
 *
 * @param[in] data The ring's data area.
 * @param[in] size The size of the data area; must be a power of two.
 * @param[in] pos The logical position, normally a head counter value.
 * @param[in] src The bytes to copy in.
 * @param[in] n The amount of bytes to copy.
 */
void ShmRingWriteAt(uint8_t *data, uint32_t size, uint32_t pos, const void *src, uint32_t n);
/**
 * @brief Copy @p n bytes out of the ring from logical position @p pos, wrapping at the end.
 *
 * Does not check for data and does not move any counter, so the caller keeps its own cursor
 * and publishes it with ShmRingPublishTail().
 *
 * @param[in] data The ring's data area.
 * @param[in] size The size of the data area; must be a power of two.
 * @param[in] pos The logical position, normally a tail counter value.
 * @param[out] dst The buffer to copy to.
 * @param[in] n The amount of bytes to copy.
 */
void ShmRingReadAt(const uint8_t *data, uint32_t size, uint32_t pos, void *dst, uint32_t n);

/**
 * @brief Write as many of @p len bytes as fit, then publish them.
 *
 * @param[in] r The ring header.
 * @param[in] data The ring's data area.
 * @param[in] src The bytes to write.
 * @param[in] len The amount of bytes to write.
 * @return The number of bytes written, which is less than @p len if the ring filled up.
 */
uint32_t ShmRingPush(ShmRingHdr *r, uint8_t *data, const void *src, uint32_t len);
/**
 * @brief Read up to @p len waiting bytes, then release them.
 *
 * @param[in] r The ring header.
 * @param[in] data The ring's data area.
 * @param[out] dst The buffer to read into.
 * @param[in] len The amount of bytes to read.
 * @return The number of bytes read, which is less than @p len if the ring ran dry.
 */
uint32_t ShmRingPop(ShmRingHdr *r, const uint8_t *data, void *dst, uint32_t len);

#ifdef __cplusplus
}
#endif
#endif
