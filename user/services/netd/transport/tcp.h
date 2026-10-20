#ifndef TCP_H
#define TCP_H

#include "../common/globals.h"
#include "../common/timer.h"
#include <types.h>

typedef struct __attribute__((packed)) {
    NetPort src_port;
    NetPort dst_port;
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
static inline bool SeqLt(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
static inline bool SeqLeq(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }
static inline uint32_t SeqMax(uint32_t a, uint32_t b) { return SeqLt(a, b) ? b : a; }

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

/* On-the-wire option kinds (IANA registry) */
#define TCP_OPT_KIND_EOL 0
#define TCP_OPT_KIND_NOP 1
#define TCP_OPT_KIND_MSS 2

/* Internal presence bitmask */
#define TCP_OPT_MSS_BIT (1u << 0)

#define TCP_MAX_PCB 128
#define TCP_MSS 1460
#define TCP_RTO_MAX 60000
#define TCP_OOO_MAX 8
#define TCP_MAX_OPTS 40
#define TCP_SND_BUF (1024 * 32)
#define TCP_RCV_BUF (1024 * 32)
#define TCP_BUF_BUDGET (2 * 1024 * 1024)
#define TCP_MSL_MS 30000
#define TCP_TIME_WAIT_MS (2 * TCP_MSL_MS)
#define TCP_SYN_RETRIES 6
#define TCP_LISTEN_BACKLOG 16
#define TCP_FIN_WAIT2_MS 60000
#define TCP_DATA_RETRIES 12

typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,    /* sent SYN waiting for ACK */
    TCP_ESTABLISHED, /* connection established */
    TCP_FIN_WAIT_1,  /* we sent FIN, waiting for ack of it */
    TCP_FIN_WAIT_2,  /* our FIN acked, waiting for their FIN */
    TCP_TIME_WAIT,   /* got their FIN, linger before close */
    TCP_CLOSE_WAIT,  /* they sent FIN first (passive close) */
    TCP_LAST_ACK,    /* passive close: we sent our FIN, waiting for ack */
    TCP_CLOSING,     /* simultaneous close: both FINs out, ours not yet acked */
    TCP_LISTENING,
    TCP_SYN_RCVD
} TcpState;

typedef struct {
    uint32_t start;
    uint32_t end;
} TcpOooRange;

typedef struct {
    void (*on_established)(int slot);
    void (*on_data)(int slot);
    void (*on_writable)(int slot);
    void (*on_close)(int slot);
    void (*on_closed)(int slot, int err);
} TcpCallbacks;

typedef struct {
    Ipv4Addr local_ip;
    NetPort local_port;
    Ipv4Addr remote_ip;
    NetPort remote_port;
    TcpState state;
    uint32_t snd_nxt;
    uint32_t snd_una;
    uint32_t snd_max; /* highest sequence number ever sent, one past the last byte */
    uint32_t rcv_nxt;
    uint32_t rcv_rsq;
    uint16_t snd_wnd;
    uint32_t rcv_adv;
    TcpOooRange ranges[TCP_OOO_MAX];
    size_t nranges;
    bool active;
    bool in_recovery;
    bool fin_pending;
    uint8_t *snd_buf;
    size_t snd_size; // power of two
    size_t buffered_bytes;
    uint8_t *rcv_buf;
    size_t rcv_size; // power of two
    const TcpCallbacks *cb;
    void *ctx;
    bool snd_blocked;
    int backlog; /* listeners: max connections in SYN_RCVD */
    int parent;  /* accepted children: listener slot + 1, else 0 */
    Timer rto_timer;
    Timer tw_timer;
    Timer persist_timer;
    Timer fin_wait2_timer;
    Duration persist_ms;
    bool fin_sent;
    Duration rto_ms;  /* current backoff value */
    uint32_t fin_seq; /* the FIN's position in sequence space */
    bool fin_seen;    /* have we been told about a FIN at all? */
    bool fin_rcvd;    /* has the FIN been consumed in order? */
    Duration srtt;
    Duration rttvar;
    bool rtt_valid;     /* have we taken the first RTT sample? */
    Duration rtt_start; /* when we sent the timed segment (NetNowMs) */
    uint32_t rtt_seq;   /* which byte we're waiting for the ACK to pass */
    bool rtt_timing;    /* is a stopwatch currently running? */
    uint16_t snd_mss;
    int retries;
    uint32_t cwnd;
    uint32_t ssthresh;
    uint32_t max_snd_wnd;
    uint32_t dupacks;
    uint32_t recover;
} TcpPcb;

typedef struct {
    Ipv4Addr src_ip;
    NetPort src_port;
    NetPort dst_port;
    uint32_t seq; /* their sequence number */
    uint32_t ack; /* their acknowledgement number */
    uint8_t flags;
    const uint8_t *payload;
    uint16_t payload_len;
    uint16_t window;
    uint16_t opts_present;
    uint16_t mss;
} TcpSegment;

typedef struct {
    uint16_t opts_present; /* which options to emit */
    uint16_t mss;          /* value when MSS bit set */
} TcpOptsOut;

_Static_assert((TCP_SND_BUF & (TCP_SND_BUF - 1)) == 0, "TCP_SND_BUF must be power of two");
_Static_assert((TCP_RCV_BUF & (TCP_RCV_BUF - 1)) == 0, "TCP_RCV_BUF must be power of two");

int TcpConnect(Ipv4Addr remote_ip, NetPort remote_port, const TcpCallbacks *cb, void *ctx);
void TcpRx(Ipv4Addr src_ip, Ipv4Addr dst_ip, const uint8_t *data, uint16_t len);
void TcpCcInit(TcpPcb *pcb);
int TcpSend(int idx, const uint8_t *data, uint16_t len);
int TcpRecv(int idx, uint8_t *buf, uint16_t sz);
int TcpListen(int port, int backlog, const TcpCallbacks *cb, void *ctx);
void *TcpGetCtx(int slot);
void TcpSetCtx(int slot, void *ctx);
int TcpClose(int idx);

#endif