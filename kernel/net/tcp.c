#include <kernel/tcp.h>
#include <kernel/ip.h>
#include <kernel/rtl8139.h>
#include <kernel/syslog.h>

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_off;   /* high 4 bits: header length in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_hdr_t;

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint32_t swap32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | ((v >> 24) & 0xFF);
}

static uint32_t checksum_accum(uint32_t sum, const void* data, int len) {
    const uint8_t* p = (const uint8_t*)data;
    while (len > 1) { sum += (uint16_t)((p[0] << 8) | p[1]); p += 2; len -= 2; }
    if (len == 1) sum += (uint16_t)(p[0] << 8);
    return sum;
}
static uint16_t checksum_finish(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ── Single static connection state ─────────────────────────────── */
static tcp_state_t state = TCP_CLOSED;
static uint8_t  remote_ip[4];
static uint16_t remote_port;
static uint16_t local_port;
static uint32_t our_seq;    /* next byte WE will send */
static uint32_t our_ack;    /* next byte we EXPECT from remote (their seq) */

void tcp_init(void) {
    state = TCP_CLOSED;
}

tcp_state_t tcp_get_state(void) { return state; }

static uint32_t initial_seq(void) {
    uint8_t mac[6];
    rtl8139_get_mac(mac);
    extern uint64_t irq_get_ticks(void);
    uint32_t seed = (uint32_t)irq_get_ticks();
    seed ^= ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    return seed;
}

/* Builds and sends one TCP segment. payload may be NULL/0-length for
 * pure control segments (SYN, ACK, FIN). Does not update our_seq —
 * callers advance it themselves based on what they actually sent, so
 * this stays a dumb "send exactly this" primitive. */
static void send_segment(uint32_t seq, uint32_t ack, uint8_t flags,
                          const void* payload, uint16_t payload_len) {
    static uint8_t pkt[1500];
    uint16_t seg_len = (uint16_t)(sizeof(tcp_hdr_t) + payload_len);

    tcp_hdr_t* tcp = (tcp_hdr_t*)pkt;
    tcp->src_port = swap16(local_port);
    tcp->dst_port = swap16(remote_port);
    tcp->seq = swap32(seq);
    tcp->ack = swap32(ack);
    tcp->data_off = (uint8_t)((sizeof(tcp_hdr_t) / 4) << 4);
    tcp->flags = flags;
    tcp->window = swap16(4096);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    uint8_t* body = pkt + sizeof(tcp_hdr_t);
    for (uint16_t i = 0; i < payload_len; i++) body[i] = ((const uint8_t*)payload)[i];

    /* TCP checksum: same pseudo-header shape as UDP's (src/dst IP,
     * zero byte, protocol, TCP segment length), covering the full
     * header+payload. */
    uint8_t our_ip[4];
    ip_get_our_ip(our_ip);
    uint32_t sum = 0;
    sum = checksum_accum(sum, our_ip, 4);
    sum = checksum_accum(sum, remote_ip, 4);
    uint8_t proto_word[2] = {0, IP_PROTO_TCP};
    sum = checksum_accum(sum, proto_word, 2);
    uint16_t len_be = swap16(seg_len);
    sum = checksum_accum(sum, &len_be, 2);
    sum = checksum_accum(sum, pkt, seg_len);
    tcp->checksum = swap16(checksum_finish(sum));

    ip_send(remote_ip, IP_PROTO_TCP, pkt, seg_len);
}

int tcp_connect(const uint8_t dest_ip[4], uint16_t dest_port) {
    if (state != TCP_CLOSED) return -1;

    for (int i = 0; i < 4; i++) remote_ip[i] = dest_ip[i];
    remote_port = dest_port;
    local_port = 44000; /* fixed ephemeral port — fine for one connection at a time */
    our_seq = initial_seq();
    our_ack = 0;

    send_segment(our_seq, 0, TCP_FLAG_SYN, 0, 0);
    our_seq++; /* SYN itself consumes one sequence number */
    state = TCP_SYN_SENT;
    syslog_write("TCP", "SYN sent");
    return 0;
}

void tcp_close(void) {
    if (state != TCP_ESTABLISHED) return;
    send_segment(our_seq, our_ack, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    our_seq++; /* FIN also consumes one sequence number */
    state = TCP_FIN_WAIT_1;
    syslog_write("TCP", "FIN sent");
}

void tcp_handle_segment(const uint8_t src_ip[4], const uint8_t* data, uint16_t len) {
    if (len < sizeof(tcp_hdr_t)) return;
    const tcp_hdr_t* tcp = (const tcp_hdr_t*)data;

    /* Only interested in segments matching our one active connection. */
    if (state == TCP_CLOSED) return;
    if (swap16(tcp->src_port) != remote_port) return;
    for (int i = 0; i < 4; i++) if (src_ip[i] != remote_ip[i]) return;

    uint8_t flags = tcp->flags;
    uint32_t seg_seq = swap32(tcp->seq);

    if (state == TCP_SYN_SENT) {
        if ((flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK)) {
            our_ack = seg_seq + 1; /* SYN consumes one sequence number on their side too */
            send_segment(our_seq, our_ack, TCP_FLAG_ACK, 0, 0);
            state = TCP_ESTABLISHED;
            syslog_write("TCP", "connection established");
        } else if (flags & TCP_FLAG_RST) {
            state = TCP_CLOSED;
            syslog_write("TCP", "connection refused (RST)");
        }
        return;
    }

    if (state == TCP_FIN_WAIT_1) {
        if (flags & TCP_FLAG_ACK) state = TCP_FIN_WAIT_2;
        if (flags & TCP_FLAG_FIN) {
            our_ack = seg_seq + 1;
            send_segment(our_seq, our_ack, TCP_FLAG_ACK, 0, 0);
            state = TCP_TIME_WAIT; /* real TCP waits here; we just treat it as done */
            syslog_write("TCP", "connection closed gracefully");
        }
        return;
    }

    if (state == TCP_FIN_WAIT_2) {
        if (flags & TCP_FLAG_FIN) {
            our_ack = seg_seq + 1;
            send_segment(our_seq, our_ack, TCP_FLAG_ACK, 0, 0);
            state = TCP_TIME_WAIT;
            syslog_write("TCP", "connection closed gracefully");
        }
        return;
    }

    /* TCP_ESTABLISHED data handling: stage 5b. */
}
