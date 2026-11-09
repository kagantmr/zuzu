/**
 * @file
 * @brief VideoCore property-mailbox protocol ("/svc/mbox").
 *
 * mboxdrv owns the BCM2711 mailbox and runs one property tag per request.
 * Requests and replies travel in the message buffer.
 */

#ifndef DEV_PROTOCOLS_MBOX_H
#define DEV_PROTOCOLS_MBOX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zuzu/err.h>
#include <zuzu/msg.h>
#include <zuzu/zuzu.h>

/** @brief Service path of the mailbox driver. */
#define MBOX_SERVICE "/svc/mbox"

/** @brief Run one property tag. MboxRequest in, MboxReply out. */
#define MBOX_CMD_PROPERTY 1

/** @brief Largest tag value buffer, in bytes. */
#define MBOX_VALUE_MAX 256u

/** @brief Property tag: board MAC address, 6 bytes in wire order. */
#define MBOX_TAG_GET_MAC 0x00010003u
/** @brief Property tag: board serial number, 8 bytes. */
#define MBOX_TAG_GET_SERIAL 0x00010004u
/** @brief Property tag: firmware revision, 4 bytes. */
#define MBOX_TAG_GET_FW_REV 0x00000001u
/** @brief Property tag: board revision, 4 bytes. */
#define MBOX_TAG_GET_BOARD_REV 0x00010002u

/** @brief A request, in the message buffer. */
typedef struct {
    uint32_t cmd;                  /**< MBOX_CMD_PROPERTY. */
    uint32_t tag;                  /**< Property tag id. */
    uint32_t buf_len;              /**< Size of the tag's value buffer, at most MBOX_VALUE_MAX. */
    uint32_t req_len;              /**< Bytes of @c value sent to the firmware. */
    uint8_t value[MBOX_VALUE_MAX]; /**< Request value. */
} MboxRequest;

/** @brief A reply, in the message buffer. */
typedef struct {
    Err status;                    /**< ZUZU_OK, ERR_IO if the firmware refused, or another error. */
    uint32_t resp_len;             /**< Bytes of @c value the firmware filled in. */
    uint8_t value[MBOX_VALUE_MAX]; /**< Response value. */
} MboxReply;

_Static_assert(sizeof(MboxRequest) <= 512, "MboxRequest must fit the message buffer");
_Static_assert(sizeof(MboxReply) <= 512, "MboxReply must fit the message buffer");

/**
 * @brief Runs one property tag through mboxdrv.
 *
 * @param[in]     port     Port of MBOX_SERVICE.
 * @param[in]     tag      Property tag id.
 * @param[in,out] value    Request value in, response value out.
 * @param[in]     buf_len  Size of @p value, at most MBOX_VALUE_MAX.
 * @param[in]     req_len  Bytes of @p value to send.
 * @param[out]    resp_len Receives the response length; may be NULL.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADARG @p buf_len or @p req_len is too large.
 * @retval ERR_IO     The firmware rejected the tag.
 */
static inline Err MboxProperty(Handle port, uint32_t tag, void *value, uint32_t buf_len,
                               uint32_t req_len, uint32_t *resp_len)
{
    if (buf_len > MBOX_VALUE_MAX || req_len > buf_len)
        return ERR_BADARG;

    MboxRequest *req = (MboxRequest *)GetMessageBox();
    req->cmd = MBOX_CMD_PROPERTY;
    req->tag = tag;
    req->buf_len = buf_len;
    req->req_len = req_len;
    memcpy(req->value, value, req_len);

    uint32_t len = 0;
    Err rc = CallPort(port, (uint32_t)(offsetof(MboxRequest, value) + req_len), -1, &len, NULL);
    if (rc != ZUZU_OK)
        return rc;
    if (len < offsetof(MboxReply, value))
        return ERR_IO;

    const MboxReply *rep = (const MboxReply *)GetMessageBox();
    if (rep->status != ZUZU_OK)
        return rep->status;
    uint32_t n = rep->resp_len < buf_len ? rep->resp_len : buf_len;
    memcpy(value, rep->value, n);
    if (resp_len)
        *resp_len = rep->resp_len;
    return ZUZU_OK;
}

/**
 * @brief Asks the firmware for the board MAC address.
 *
 * @param[in]  port Port of MBOX_SERVICE.
 * @param[out] mac  Receives 6 bytes, first byte first on the wire.
 * @return ZUZU_OK, or an error from MboxProperty().
 */
static inline Err MboxGetMac(Handle port, uint8_t mac[6])
{
    uint8_t buf[8] = {0};
    Err rc = MboxProperty(port, MBOX_TAG_GET_MAC, buf, sizeof(buf), 0, NULL);
    if (rc == ZUZU_OK)
        memcpy(mac, buf, 6);
    return rc;
}

#ifdef __cplusplus
}
#endif

#endif /* DEV_PROTOCOLS_MBOX_H */
