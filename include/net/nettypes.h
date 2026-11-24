/**
 * @file
 * @brief Basic network types shared by netd, its protocols and NIC drivers.
 */

#ifndef NET_NETTYPES_H
#define NET_NETTYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Length of an Ethernet MAC address in bytes. */
#define MAC_ADDR_LEN 6

/**
 * @brief Expands an IPv4 address in network byte order into four printf arguments, for
 * "%u.%u.%u.%u".
 */
#define IP4(x)                                                                                     \
    (unsigned)((x) & 0xff), (unsigned)(((x) >> 8) & 0xff), (unsigned)(((x) >> 16) & 0xff),         \
        (unsigned)(((x) >> 24) & 0xff)

/** @brief IPv4 address in network byte order. */
typedef uint32_t Ipv4Addr;
/** @brief TCP or UDP port. */
typedef uint16_t NetPort;
/** @brief Ethernet MAC address. */
typedef uint8_t MacAddr[MAC_ADDR_LEN];


/**
 * @brief Format @p ip into @p buf
 * @param[in] ip the numerical IP address
 * @param[out] buf The string holding the IP address
 *
 */
void Ipv4ToStr(Ipv4Addr ip, char *buf);

/**
 * @brief Parse @p into @p out
 * 
 * @param[in] s The IP string
 * @param[out] out Pointer to the numerical address to write to.
 */
bool Ipv4ToBytes(const char *s, Ipv4Addr *out);

#ifdef __cplusplus
}
#endif

#endif // NET_NETTYPES_H
