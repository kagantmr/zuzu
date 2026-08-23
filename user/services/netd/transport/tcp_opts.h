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

#endif /* TCP_OPTIONS_H */