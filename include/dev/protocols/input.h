/**
 * @file
 * @brief Input events, routed through gfxsvc ("/svc/gfx").
 *
 * Input drivers send batches of events to gfxsvc with INPUT_CMD_PUSH. gfxsvc turns relative pointer
 * motion into an absolute position on the screen, handles Ctrl+Alt+F1..F8 itself, and copies the
 * rest into the active VT's ring (see FB_CMD_INPUT_OPEN in fb.h).
 */

#ifndef INPUT_PROTOCOL_H
#define INPUT_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <zuzu/msg.h>
#include <zuzu/shm_ring.h>

/** @brief A key went down or up. @c code is a HID usage (dev/hid.h), @c value 1 or 0. */
#define INPUT_EV_KEY 1u
/** @brief Relative pointer motion. @c code is INPUT_REL_*, @c value the delta; Y grows downward. */
#define INPUT_EV_REL 2u
/** @brief A pointer button went down or up. @c code is INPUT_BTN_*, @c value 1 or 0. */
#define INPUT_EV_BTN 3u
/** @brief Absolute pointer position in pixels. @c code is INPUT_ABS_*. Sent by gfxsvc only. */
#define INPUT_EV_ABS 4u
/** @brief Ends one report; everything since the previous one happened at once. */
#define INPUT_EV_SYN 5u

/** @brief Horizontal axis of INPUT_EV_REL. */
#define INPUT_REL_X 0u
/** @brief Vertical axis of INPUT_EV_REL. */
#define INPUT_REL_Y 1u
/** @brief Horizontal axis of INPUT_EV_ABS. */
#define INPUT_ABS_X 0u
/** @brief Vertical axis of INPUT_EV_ABS. */
#define INPUT_ABS_Y 1u
/** @brief Left pointer button. */
#define INPUT_BTN_LEFT 0u
/** @brief Right pointer button. */
#define INPUT_BTN_RIGHT 1u
/** @brief Middle pointer button. */
#define INPUT_BTN_MIDDLE 2u

/** @brief One input event. */
typedef struct {
    uint16_t type;    /**< An INPUT_EV_* value. */
    uint16_t code;    /**< Key, axis or button, depending on @c type. */
    int32_t value;    /**< State or delta, depending on @c type. */
    uint32_t time_ms; /**< Uptime in milliseconds when the event happened. */
    uint32_t reserved; /**< Zero. */
} InputEvent;

_Static_assert(sizeof(InputEvent) == 16, "InputEvent must stay a power of two");

/**
 * @brief Driver to gfxsvc: deliver events. Request InputPush with @c count events; replies
 * {Err status}.
 */
#define INPUT_CMD_PUSH 0x201u
/** @brief Most events in one INPUT_CMD_PUSH. */
#define INPUT_PUSH_MAX 16u

/** @brief An INPUT_CMD_PUSH request. */
typedef struct {
    uint32_t cmd;                      /**< INPUT_CMD_PUSH. */
    uint32_t count;                    /**< Events in @c ev. */
    InputEvent ev[INPUT_PUSH_MAX];     /**< The events, oldest first. */
} InputPush;

_Static_assert(sizeof(InputPush) <= MSG_BUF_SIZE, "InputPush must fit the message buffer");

/** @brief Bytes of event data in an InputRing; a power of two. */
#define INPUT_RING_BYTES 2048u

/** @brief The ring gfxsvc fills for one client: one page, gfxsvc produces, the client consumes. */
typedef struct {
    ShmRingHdr hdr;                   /**< Ring counters. */
    uint8_t data[INPUT_RING_BYTES];   /**< Whole InputEvents only. */
} InputRing;

_Static_assert(sizeof(InputRing) <= 4096, "InputRing must fit in one page");

/**
 * @brief Takes whole events out of a ring.
 *
 * @param[in,out] ring The ring.
 * @param[out]    out  Receives up to @p max events.
 * @param[in]     max  Room in @p out.
 * @return The number of events read.
 */
static inline uint32_t InputRingRead(InputRing *ring, InputEvent *out, uint32_t max)
{
    uint32_t n = ShmRingAvail(&ring->hdr) / (uint32_t)sizeof(InputEvent);
    if (n > max)
        n = max;
    if (n == 0)
        return 0;
    return ShmRingPop(&ring->hdr, ring->data, out, n * (uint32_t)sizeof(InputEvent)) /
           (uint32_t)sizeof(InputEvent);
}

/**
 * @brief Adds one event to a ring if there is room for all of it.
 *
 * @param[in,out] ring The ring.
 * @param[in]     ev   The event.
 * @return true if it was added, false if the ring was full.
 */
static inline bool InputRingWrite(InputRing *ring, const InputEvent *ev)
{
    if (ShmRingFree(&ring->hdr) < sizeof(*ev))
        return false;
    ShmRingPush(&ring->hdr, ring->data, ev, (uint32_t)sizeof(*ev));
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* INPUT_PROTOCOL_H */
