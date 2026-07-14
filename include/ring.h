#ifndef ZUZU_RING_H
#define ZUZU_RING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint8_t *buf;
    size_t size;   // must be power of 2
    uint32_t head; // written by producer
    uint32_t tail; // written by consumer
} ring_t;

/**
 * @brief Initializes a ring buffer.
 *
 * @param r Pointer to the ring buffer structure to initialize.
 * @param buf Pointer to the buffer that will be used for the ring.
 * @param size Size of the buffer in bytes. Must be a power of 2.
 */
void ring_init(ring_t *r, uint8_t *buf, uint32_t size);

/**
 * @brief Pushes a byte into the ring buffer.
 *
 * @param r Pointer to the ring buffer.
 * @param byte The byte to push into the buffer.
 * @return int Returns 0 on success, or -1 if the buffer is full.
 */
int ring_push(ring_t *r, uint8_t byte); // returns 0 or -1 if full

/**
 * @brief Checks if the ring buffer is full.
 *
 * @param r Pointer to the ring buffer.
 * @param out Pointer to the variable that will receive the popped byte.
 * @return int Returns 1 if the buffer is full, 0 otherwise.
 */
int ring_pop(ring_t *r, uint8_t *out); // returns 0 or -1 if empty

/**
 * @brief Peeks at the next byte in the ring buffer without removing it.
 *
 * @param r Pointer to the ring buffer.
 * @param out Pointer to the variable that will receive the peeked byte.
 * @return int Returns 0 on success, or -1 if the buffer is empty.
 */
int ring_peek(const ring_t *r, uint8_t *out);

/**
 * @brief Returns the number of bytes available in the ring buffer.
 *
 * @param r Pointer to the ring buffer.
 * @return uint32_t The number of bytes available in the buffer.
 */
uint32_t ring_avail(const ring_t *r);

/**
 * @brief Checks if the ring buffer is full.
 *
 * @param r Pointer to the ring buffer.
 * @return int Returns 1 if the buffer is full, 0 otherwise.
 */
int ring_full(const ring_t *r);

/**
 * @brief Pushes multiple bytes into the ring buffer from a source buffer.
 *
 * @param r Pointer to the ring buffer.
 * @param src Pointer to the source buffer containing the bytes to push.
 * @param len The number of bytes to push from the source buffer.
 * @return uint32_t The number of bytes successfully pushed into the ring buffer.
 */
uint32_t ring_push_buf(ring_t *r, const uint8_t *src, uint32_t len);

/**
 * @brief Pops multiple bytes from the ring buffer into a destination buffer.
 *
 * @param r Pointer to the ring buffer.
 * @param dst Pointer to the destination buffer where the popped bytes will be stored.
 * @param len The maximum number of bytes to pop into the destination buffer.
 *
 * @return uint32_t The number of bytes successfully popped from the ring buffer.
 */
uint32_t ring_pop_buf(ring_t *r, uint8_t *dst, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // ZUZU_RING_H