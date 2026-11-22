#include "nettypes.h"
#include <stdbool.h>
#include <string.h>

bool Ipv4ToBytes(const char *s, Ipv4Addr *out)
{
    uint32_t addr = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t v = 0;
        int digits = 0;
        for (; *s >= '0' && *s <= '9'; s++, digits++) {
            v = (v * 10) + (uint32_t)(*s - '0');
            if (v > 255)
                return false;
        }
        if (digits == 0 || digits > 3 || *s != (i == 3 ? '\0' : '.'))
            return false;
        addr |= v << (8 * i);
        if (i < 3)
            s++;
    }
    *out = addr;
    return true;
}

/**
 * @brief Parse @p ip into @p buf; false on anything else.
 * @param[in] s the numerical IP address
 * @param[out] buf The string holding the IP address
 */
void Ipv4ToStr(Ipv4Addr ip, char *buf) { (void)snprintf(buf, 16, "%u.%u.%u.%u", IP4(ip)); }
