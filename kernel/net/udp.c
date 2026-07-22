#include <kernel/udp.h>
#include <kernel/ip.h>
#include <kernel/syslog.h>

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_hdr_t;

volatile int udp_packet_seen = 0;
uint16_t udp_last_src_port = 0;

static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

/* Same running-sum accumulator as ip.c's checksum16, but callable
 * incrementally so the pseudo-header + real header + payload can all
 * feed into one checksum without building one giant contiguous buffer
 * just to reuse ip.c's version, which only takes a single pointer. */
static uint32_t checksum_accum(uint32_t sum, const void* data, int len) {
    const uint8_t* p = (const uint8_t*)data;
    while (len > 1) {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2; len -= 2;
    }
    if (len == 1) sum += (uint16_t)(p[0] << 8);
    return sum;
}
static uint16_t checksum_finish(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

int udp_send(const uint8_t dest_ip[4], uint16_t dest_port, uint16_t src_port,
             const void* payload, uint16_t payload_len) {
    static uint8_t pkt[1500];
    uint16_t udp_len = (uint16_t)(sizeof(udp_hdr_t) + payload_len);
    if ((uint32_t)udp_len > sizeof(pkt)) return -1;

    udp_hdr_t* udp = (udp_hdr_t*)pkt;
    udp->src_port = swap16(src_port);
    udp->dst_port = swap16(dest_port);
    udp->length = swap16(udp_len);
    udp->checksum = 0;

    uint8_t* body = pkt + sizeof(udp_hdr_t);
    for (uint16_t i = 0; i < payload_len; i++) body[i] = ((const uint8_t*)payload)[i];

    /* UDP checksum covers a pseudo-header (src IP, dst IP, zero byte,
     * protocol, UDP length) in addition to the real UDP header+payload
     * — this is why it can't just reuse ip.c's plain checksum16 on the
     * packet bytes alone. */
    uint8_t our_ip[4];
    ip_get_our_ip(our_ip);
    uint32_t sum = 0;
    sum = checksum_accum(sum, our_ip, 4);
    sum = checksum_accum(sum, dest_ip, 4);
    uint8_t proto_word[2] = {0, IP_PROTO_UDP};
    sum = checksum_accum(sum, proto_word, 2);
    uint16_t len_be = swap16(udp_len);
    sum = checksum_accum(sum, &len_be, 2);
    sum = checksum_accum(sum, pkt, udp_len);
    uint16_t csum = checksum_finish(sum);
    /* All-zero is reserved to mean "no checksum computed" in UDP —
     * a genuine computed checksum that happens to equal zero must be
     * sent as all-ones instead, or a receiver would misinterpret it. */
    if (csum == 0) csum = 0xFFFF;
    udp->checksum = swap16(csum);

    return ip_send(dest_ip, IP_PROTO_UDP, pkt, udp_len);
}

void udp_handle_packet(const uint8_t src_ip[4], const uint8_t* data, uint16_t len) {
    (void)src_ip;
    if (len < sizeof(udp_hdr_t)) return;
    const udp_hdr_t* udp = (const udp_hdr_t*)data;
    udp_packet_seen = 1;
    udp_last_src_port = swap16(udp->src_port);
    syslog_write("UDP", "datagram received");
}
