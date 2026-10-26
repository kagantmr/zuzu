/**
 * @file
 * @brief devsvc protocol: asking rootsvc's device service for a device by compatible string.
 */

#ifndef DEVMGR_PROTOCOL_H
#define DEVMGR_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <types.h>
#include <zuzu/msg.h>
#include <zuzu/zuzu.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief devsvc commands. */
typedef enum {
    DEVM_REQUEST, /**< Ask for the first device matching one of a list of compatible strings. */
    DEVM_REQUEST_NTH /**< Ask for the nth device, in boot-list order, matching one of the strings. */
} DevmRequestType;

/** @brief Unused. */
#define DEVM_COMPAT_MAX 32
/** @brief Most compatible strings one request may carry. */
#define DEVM_MAX_COMPAT 8

/**
 * @brief A request as devsvc unpacks it.
 *
 * On the wire it is cmd and count as two 32-bit words (plus nth for
 * DEVM_REQUEST_NTH), then @c count NUL-terminated strings back to back.
 */
typedef struct {
    DevmRequestType cmd;                  /**< DEVM_REQUEST or DEVM_REQUEST_NTH. */
    uint32_t nth;                         /**< DEVM_REQUEST_NTH only: matches to skip, 0 for the first. */
    uint32_t count;                       /**< Number of strings, 1 to DEVM_MAX_COMPAT. */
    const char *strings[DEVM_MAX_COMPAT]; /**< The strings, pointing into the message buffer. */
} DevmRequest;

/**
 * @brief Sends a device request to devsvc and unpacks the reply.
 *
 * @param[in]  devsvc_port Handle to /svc/devsvc.
 * @param[in]  cmd         DEVM_REQUEST or DEVM_REQUEST_NTH.
 * @param[in]  nth         DEVM_REQUEST_NTH only: matches to skip, 0 for the first.
 * @param[in]  compats     Compatible strings.
 * @param[in]  count       Number of strings, 1 to DEVM_MAX_COMPAT.
 * @param[out] out_matched Receives the device's index in the boot device list; may be NULL.
 * @return The device handle, or a negative error code.
 * @retval ERR_NOENT    No device matched.
 * @retval ERR_BADARG   @p count is 0 or above DEVM_MAX_COMPAT.
 * @retval ERR_OVERFLOW The strings do not fit in the message buffer.
 */
static inline Handle DevmCall(Handle devsvc_port, uint32_t cmd, uint32_t nth,
                              const char *const *compats, uint32_t count, uint32_t *out_matched)
{
    if (count == 0 || count > DEVM_MAX_COMPAT)
        return ERR_BADARG;
    char *b = GetMessageBox();
    memcpy(b + 0, &cmd, 4);
    memcpy(b + 4, &count, 4);
    uint32_t off = 8;
    if (cmd == DEVM_REQUEST_NTH) {
        memcpy(b + off, &nth, 4);
        off += 4;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t n = (uint32_t)strlen(compats[i]) + 1;
        if (off + n > MSG_BUF_SIZE)
            return ERR_OVERFLOW;
        memcpy(b + off, compats[i], n);
        off += n;
    }

    Handle found = -1;
    Err rc = CallPort(devsvc_port, off, -1, NULL, &found);
    if (rc != ZUZU_OK)
        return (Handle)rc;
    if (found < 0)
        return ERR_NOENT;
    if (out_matched)
        memcpy(out_matched, GetMessageBox(), sizeof(uint32_t));
    return found;
}

/**
 * @brief Asks devsvc for a device's memory object.
 *
 * The strings are tried in order; the first one that matches a device in the
 * boot device list wins. The handle carries PERM_MAP | PERM_WRITE | PERM_TXFR.
 *
 * @param[in]  devsvc_port Handle to /svc/devsvc.
 * @param[in]  compats     Compatible strings, most specific first.
 * @param[in]  count       Number of strings, 1 to DEVM_MAX_COMPAT.
 * @param[out] out_matched Receives the device's index in the boot device list; may be NULL.
 * @return The device handle, or a negative error code.
 * @retval ERR_NOENT    No device matched.
 * @retval ERR_BADARG   @p count is 0 or above DEVM_MAX_COMPAT.
 * @retval ERR_OVERFLOW The strings do not fit in the message buffer.
 */
static inline Handle RequestDevice(Handle devsvc_port, const char *const *compats, uint32_t count,
                                   uint32_t *out_matched)
{
    return DevmCall(devsvc_port, DEVM_REQUEST, 0, compats, count, out_matched);
}

/**
 * @brief Asks devsvc for the nth device matching any of the strings.
 *
 * Devices are counted in boot-list order, and a device that matches more than
 * one string counts once. Use it for several instances of the same hardware,
 * such as two PL050 ports.
 *
 * @param[in]  devsvc_port Handle to /svc/devsvc.
 * @param[in]  compats     Compatible strings.
 * @param[in]  count       Number of strings, 1 to DEVM_MAX_COMPAT.
 * @param[in]  nth         Matches to skip: 0 for the first, 1 for the second.
 * @param[out] out_matched Receives the device's index in the boot device list; may be NULL.
 * @return The device handle, or a negative error code.
 * @retval ERR_NOENT    Fewer than nth + 1 devices match.
 * @retval ERR_BADARG   @p count is 0 or above DEVM_MAX_COMPAT.
 * @retval ERR_OVERFLOW The strings do not fit in the message buffer.
 */
static inline Handle RequestDeviceNth(Handle devsvc_port, const char *const *compats,
                                      uint32_t count, uint32_t nth, uint32_t *out_matched)
{
    return DevmCall(devsvc_port, DEVM_REQUEST_NTH, nth, compats, count, out_matched);
}

#ifdef __cplusplus
}
#endif

#endif
