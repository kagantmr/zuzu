#ifndef TCP_OPTIONS_H
#define TCP_OPTIONS_H

#include "tcp.h" /* for tcp_seg_t */
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Parses the options sections of a received TCP segment, populates
 * seg->opts_present and value fields.
 *
 * @param opts
 * @param len
 * @param seg
 * 
 * @return Flag dictating whether or not the parser completely parsed the segment
 */
bool tcp_parse_options(const uint8_t *opts, size_t len, TcpSegment *seg);

/**
* @brief Emits desired options into the destination buffer. 
* 
* @param dst
* @param cap
* @param opts
*
* @return bytes written, always multiple of 4. return 0 means empty options
*/
size_t tcp_build_options(uint8_t *dst, size_t cap, const TcpOptsOut *opts);

#endif /* TCP_OPTIONS_H */