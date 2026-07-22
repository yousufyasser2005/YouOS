#include <kernel/ip.h>
#include <kernel/arp.h>
#include <kernel/rtl8139.h>
#include <kernel/syslog.h>
#include <kernel/udp.h>

typedef struct __attribute__((packed)) {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
} eth_hdr_t;

#define ETHERTYPE_IPV4 0x0800

typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;    /* version (4 bits) + IHL in 32-bit words (4 bits) */
    uint8_t  tos;
    uint16_t total_len;  /* big-endian */
    uint16_t id;         /* big-endian */
    uint16_t flags_frag; /* big-endian */
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;   /* big-endian */
    uint8_t  src_ip[4];
    uint8_t  dst_ip[4];
} ip_hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum; /* big-endian */
    uint16_t id;        /* big-endian */
    uint16_t seq;       /* big-endian */
} icmp_hdr_t;

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

static uint8_t our_ip[4];
static uint8_t our_mac[6];
static uint16_t ip_id_counter = 1;

/* Set by ip_handle_frame() when an ICMP echo reply arrives — read by
 * the boot self-test in kernel_main.c, same pattern as stage 1's
 * rx_irq_seen and stage 2's arp cache check. */
volatile int icmp_echo_reply_seen = 0;

static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

static uint16_t checksum16(const void* data, int len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2; len -= 2;
    }
    if (len == 1) sum += (uint16_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void ip_init(const uint8_t ip[4]) {
    rtl8139_get_mac(our_mac);
    for (int i = 0; i < 4; i++) our_ip[i] = ip[i];
}

void ip_get_our_ip(uint8_t out[4]) {
    for (int i = 0; i < 4; i++) out[i] = our_ip[i];
}

int ip_send(const uint8_t dest_ip[4], uint8_t protocol, const void* payload, uint16_t payload_len) {
    uint8_t dest_mac[6];
    if (!arp_resolve(dest_ip, dest_mac)) {
        arp_request(dest_ip); /* kick off resolution; caller must poll + retry */
        return -1;
    }

    static uint8_t frame[1600];
    if ((uint32_t)(sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + payload_len) > sizeof(frame)) return -1;

    eth_hdr_t* eth = (eth_hdr_t*)frame;
    ip_hdr_t*  ip  = (ip_hdr_t*)(frame + sizeof(eth_hdr_t));
    uint8_t*   body = frame + sizeof(eth_hdr_t) + sizeof(ip_hdr_t);

    for (int i = 0; i < 6; i++) eth->dst_mac[i] = dest_mac[i];
    for (int i = 0; i < 6; i++) eth->src_mac[i] = our_mac[i];
    eth->ethertype = swap16(ETHERTYPE_IPV4);

    ip->ver_ihl = 0x45; /* version 4, IHL 5 (20 bytes, no options) */
    ip->tos = 0;
    ip->total_len = swap16((uint16_t)(sizeof(ip_hdr_t) + payload_len));
    ip->id = swap16(ip_id_counter++);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    for (int i = 0; i < 4; i++) ip->src_ip[i] = our_ip[i];
    for (int i = 0; i < 4; i++) ip->dst_ip[i] = dest_ip[i];
    ip->checksum = swap16(checksum16(ip, sizeof(ip_hdr_t)));

    for (uint16_t i = 0; i < payload_len; i++) body[i] = ((const uint8_t*)payload)[i];

    return rtl8139_send(frame, (uint16_t)(sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + payload_len));
}

static void handle_icmp(const ip_hdr_t* ip, const uint8_t* icmp_data, uint16_t icmp_len) {
    if (icmp_len < sizeof(icmp_hdr_t)) return;
    const icmp_hdr_t* icmp = (const icmp_hdr_t*)icmp_data;

    if (icmp->type == ICMP_ECHO_REPLY) {
        icmp_echo_reply_seen = 1;
        char msg[48];
        int p = 0;
        const char* pfx = "echo reply from ";
        for (int i = 0; pfx[i]; i++) msg[p++] = pfx[i];
        for (int i = 0; i < 4; i++) {
            int v = ip->src_ip[i];
            if (v >= 100) msg[p++] = (char)('0' + v/100);
            if (v >= 10)  msg[p++] = (char)('0' + (v/10)%10);
            msg[p++] = (char)('0' + v%10);
            if (i < 3) msg[p++] = '.';
        }
        msg[p] = 0;
        syslog_write("ICMP", msg);
        return;
    }

    if (icmp->type == ICMP_ECHO_REQUEST) {
        /* Answer it — build a reply with the same id/seq/payload,
         * type flipped to ECHO_REPLY, back to whoever asked. */
        static uint8_t reply[1500];
        if (icmp_len > sizeof(reply)) return;
        for (uint16_t i = 0; i < icmp_len; i++) reply[i] = icmp_data[i];
        icmp_hdr_t* rh = (icmp_hdr_t*)reply;
        rh->type = ICMP_ECHO_REPLY;
        rh->checksum = 0;
        rh->checksum = swap16(checksum16(reply, icmp_len));
        ip_send(ip->src_ip, IP_PROTO_ICMP, reply, icmp_len);
    }
}

void ip_handle_frame(const uint8_t* data, uint16_t len) {
    if (len < sizeof(ip_hdr_t)) return;
    const ip_hdr_t* ip = (const ip_hdr_t*)data;

    uint8_t ihl_bytes = (uint8_t)((ip->ver_ihl & 0x0F) * 4);
    if (ihl_bytes < sizeof(ip_hdr_t) || len < ihl_bytes) return;

    /* Checksum is verified and logged on mismatch but NOT enforced —
     * dropping silently on a checksum error would make debugging a
     * genuine header-parsing bug indistinguishable from real network
     * corruption. Pragmatic choice for a hobby OS at this stage; a
     * production stack would drop. */
    uint16_t stored_sum = swap16(ip->checksum);
    ip_hdr_t copy = *ip;
    copy.checksum = 0;
    uint16_t computed = checksum16(&copy, ihl_bytes);
    if (computed != stored_sum) syslog_write("IP", "checksum mismatch (logged, not dropped)");

    uint16_t total_len = swap16(ip->total_len);
    if (total_len > len) return; /* truncated frame, can't trust it */

    const uint8_t* body = data + ihl_bytes;
    uint16_t body_len = (uint16_t)(total_len - ihl_bytes);

    if (ip->protocol == IP_PROTO_ICMP) handle_icmp(ip, body, body_len);
    else if (ip->protocol == IP_PROTO_UDP) udp_handle_packet(ip->src_ip, body, body_len);
    /* TCP dispatch: future stage. */
}
