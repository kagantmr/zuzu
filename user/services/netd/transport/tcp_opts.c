#include "tcp_opts.h"
#include <stdint.h>
#include <zuzu/log.h>

bool TcpParseOptions(const uint8_t *opts, size_t len, TcpSegment *seg)
{
    size_t i = 0;
    while (i < len) {
        uint8_t kind = opts[i];
        if (kind == TCP_OPT_KIND_EOL) {
            return true;
        } else if (kind == TCP_OPT_KIND_NOP) {
            i += 1;
            continue;
        } else {
            if (i + 1 < len) {
                uint8_t length = opts[i + 1];
                if (length < 2 || i + length > len)
                    return false; /* malformed option length */
                switch (kind) {
                case TCP_OPT_KIND_MSS: {
                    if (length != 4)
                        break;
                    uint8_t hi = opts[i + 2], lo = opts[i + 3];
                    seg->mss = ((uint16_t)hi << 8) | lo;
                    seg->opts_present |= TCP_OPT_MSS_BIT;
                } break;
                default:
                    break; /* silent skip per RFC 793 */
                }
                i += length;
            } else {
                return false; /* truncated: no length byte */
            }
        }
    }
    return true;
}

size_t TcpBuildOptions(uint8_t *dst, size_t cap, const TcpOptsOut *opts)
{
    size_t len = 0;
    if ((opts->opts_present & TCP_OPT_MSS_BIT) && cap - len >= 4) {
        dst[len++] = TCP_OPT_KIND_MSS;
        dst[len++] = 4;
        dst[len++] = opts->mss >> 8;
        dst[len++] = opts->mss & 0xFF;
    }
    while (len % 4 != 0 && len < cap)
        dst[len++] = TCP_OPT_KIND_NOP;
    return len;
}

