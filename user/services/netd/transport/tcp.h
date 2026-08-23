#ifndef TCP_H
#define TCP_H

#include "../common/globals.h"
#include "../common/timer.h"
#include <zuzu/types.h>

typedef struct __attribute__((packed)) {
  port_t src_port;
  port_t dst_port;
  uint32_t seq;
  uint32_t ack;
  uint8_t data_offset;
  uint8_t flags;
  uint16_t window;
  uint16_t checksum;
  uint16_t urgent_ptr;
} TcpHdr;

_Static_assert(sizeof(TcpHdr) == 20, "TCP header size");

/* wraparound-safe sequence number comparisons (RFC 1323 style) */
#define seq_lt(a, b) ((int32_t)((a) - (b)) < 0)
#define seq_leq(a, b) ((int32_t)((a) - (b)) <= 0)
#define seq_max(a, b) (seq_lt((a), (b)) ? (b) : (a))

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

/* On-the-wire option kinds (IANA registry) */
#define TCP_OPT_KIND_EOL 0
#define TCP_OPT_KIND_NOP 1
#define TCP_OPT_KIND_MSS 2

/* Internal presence bitmask for tcp_seg_t.opts_present */
#define TCP_OPT_MSS_BIT (1u << 0)

#define TCP_MAX_PCB 64
#define TCP_MSS 1460
#define TCP_RTO_MAX 60000
#define TCP_OOO_MAX 8
#define TCP_SND_BUF (1024 * 32)
#define TCP_RCV_BUF (1024 * 32)
#define TCP_TIME_WAIT_MS                                                       \
  5000 /* linger 5s before freeing (real TCP uses 2*MSL ~minutes) */

typedef enum {
  TCP_CLOSED = 0,
  TCP_SYN_SENT,    /* sent SYN waiting for ACK */
  TCP_ESTABLISHED, /* connection established */
  TCP_FIN_WAIT_1,  /* we sent FIN, waiting for ack of it */
  TCP_FIN_WAIT_2,  /* our FIN acked, waiting for their FIN */
  TCP_TIME_WAIT,   /* got their FIN, linger before close */
  TCP_CLOSE_WAIT,  /* they sent FIN first (passive close) */
  TCP_LAST_ACK,    /* passive close: we sent our FIN, waiting for ack */
  TCP_LISTENING,
  TCP_SYN_RCVD
} tcp_state_t;

typedef struct {
  uint32_t start;
  uint32_t end;
} TcpOooRange;

typedef struct {
  ipv4_addr_t local_ip;
  port_t local_port;
  ipv4_addr_t remote_ip;
  port_t remote_port;
  tcp_state_t state;
  uint32_t snd_nxt;
  uint32_t snd_una;
  uint32_t rcv_nxt;
  uint32_t rcv_rsq;
  uint16_t snd_wnd;
  TcpOooRange ranges[TCP_OOO_MAX];
  size_t nranges;
  bool active;
  bool fin_pending;
  uint8_t snd_buf[TCP_SND_BUF]; // size must be a power of 2
  size_t buffered_bytes;        // how many bytes are buffered?
  uint8_t rcv_buf[TCP_RCV_BUF]; // size must be a power of 2
  void (*on_data)(int slot);    // data arrival callback
  TimerHandle rto_timer;
  Duration rto_ms;  /* current backoff value */
  uint32_t fin_seq; /* the FIN's position in sequence space */
  bool fin_seen;    /* have we been told about a FIN at all? */
  Duration srtt;
  Duration rttvar;
  bool rtt_valid;     /* have we taken the first RTT sample? */
  Duration rtt_start; /* when we sent the timed segment (net_now_ms) */
  uint32_t rtt_seq;   /* which byte we're waiting for the ACK to pass */
  bool rtt_timing;    /* is a stopwatch currently running? */
} TcpPcb;

typedef struct {
  ipv4_addr_t src_ip;
  port_t src_port;
  port_t dst_port;
  uint32_t seq; /* their sequence number */
  uint32_t ack; /* their acknowledgement number */
  uint8_t flags;
  const uint8_t *payload;
  uint16_t payload_len;
  uint16_t window;
  uint16_t opts_present;
  uint16_t mss;
} TcpSegment;

_Static_assert((TCP_SND_BUF & (TCP_SND_BUF - 1)) == 0,
               "TCP_SND_BUF must be power of two");
_Static_assert((TCP_RCV_BUF & (TCP_RCV_BUF - 1)) == 0,
               "TCP_RCV_BUF must be power of two");

int tcp_connect(ipv4_addr_t remote_ip, port_t remote_port); // , callback later)
void tcp_rx(ipv4_addr_t src_ip, ipv4_addr_t dst_ip, const uint8_t *data,
            uint16_t len);
int tcp_send(int idx, const uint8_t *data, uint16_t len);
int tcp_recv(int idx, uint8_t *buf, uint16_t sz);
int tcp_listen(int port);
int tcp_close(int idx);

#endif