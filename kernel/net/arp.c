#include <kernel/arp.h>
#include <kernel/rtl8139.h>
#include <kernel/syslog.h>

/* ── Ethernet header (14 bytes) ─────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype; /* big-endian on the wire */
} eth_hdr_t;

#define ETHERTYPE_ARP 0x0806

/* ── ARP header (28 bytes for IPv4-over-Ethernet) ───────────────── */
typedef struct __attribute__((packed)) {
    uint16_t htype;      /* hardware type: 1 = Ethernet */
    uint16_t ptype;      /* protocol type: 0x0800 = IPv4 */
    uint8_t  hlen;        /* hardware addr length: 6 */
    uint8_t  plen;        /* protocol addr length: 4 */
    uint16_t oper;        /* 1 = request, 2 = reply */
    uint8_t  sender_mac[6];
    uint8_t  sender_ip[4];
    uint8_t  target_mac[6];
    uint8_t  target_ip[4];
} arp_hdr_t;

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    int     valid;
} arp_cache_entry_t;

static arp_cache_entry_t cache[ARP_CACHE_SIZE];
static uint8_t our_mac[6];

/* Manual big-endian 16-bit swap — avoids pulling in any htons()
 * dependency that may not exist in this freestanding environment. */
static inline uint16_t swap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

static int ip_eq(const uint8_t a[4], const uint8_t b[4]) {
    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];
}

static void cache_insert(const uint8_t ip[4], const uint8_t mac[6]) {
    /* Update in place if already present */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && ip_eq(cache[i].ip, ip)) {
            for (int k = 0; k < 6; k++) cache[i].mac[k] = mac[k];
            return;
        }
    }
    /* Otherwise take the first free slot, or overwrite slot 0 if full
     * (simple fixed-size cache, no LRU — fine for a handful of hosts). */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!cache[i].valid) {
            for (int k = 0; k < 4; k++) cache[i].ip[k] = ip[k];
            for (int k = 0; k < 6; k++) cache[i].mac[k] = mac[k];
            cache[i].valid = 1;
            return;
        }
    }
    for (int k = 0; k < 4; k++) cache[0].ip[k] = ip[k];
    for (int k = 0; k < 6; k++) cache[0].mac[k] = mac[k];
    cache[0].valid = 1;
}

int arp_resolve(const uint8_t ip[4], uint8_t mac_out[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].valid && ip_eq(cache[i].ip, ip)) {
            for (int k = 0; k < 6; k++) mac_out[k] = cache[i].mac[k];
            return 1;
        }
    }
    return 0;
}

void arp_init(void) {
    rtl8139_get_mac(our_mac);
    for (int i = 0; i < ARP_CACHE_SIZE; i++) cache[i].valid = 0;
}

void arp_request(const uint8_t ip[4]) {
    uint8_t frame[sizeof(eth_hdr_t) + sizeof(arp_hdr_t)];
    eth_hdr_t* eth = (eth_hdr_t*)frame;
    arp_hdr_t* arp = (arp_hdr_t*)(frame + sizeof(eth_hdr_t));

    for (int i = 0; i < 6; i++) eth->dst_mac[i] = 0xFF; /* broadcast */
    for (int i = 0; i < 6; i++) eth->src_mac[i] = our_mac[i];
    eth->ethertype = swap16(ETHERTYPE_ARP);

    arp->htype = swap16(1);
    arp->ptype = swap16(0x0800);
    arp->hlen = 6;
    arp->plen = 4;
    arp->oper = swap16(ARP_OP_REQUEST);
    for (int i = 0; i < 6; i++) arp->sender_mac[i] = our_mac[i];
    /* Sender IP left as 0.0.0.0 by the caller unless they've set one up
     * via a future IP-config step — stage 2 has no IP configuration
     * yet, so this mirrors the stage-1 self-test's hardcoded sender IP
     * only where a caller supplies one; a bare probe with 0.0.0.0 is
     * still valid ARP and commonly used for duplicate-address checks. */
    for (int i = 0; i < 4; i++) arp->sender_ip[i] = 0;
    for (int i = 0; i < 6; i++) arp->target_mac[i] = 0;
    for (int i = 0; i < 4; i++) arp->target_ip[i] = ip[i];

    rtl8139_send(frame, sizeof(frame));
}

static void handle_arp(const arp_hdr_t* arp) {
    if (swap16(arp->oper) != ARP_OP_REPLY) return; /* stage 2: replies only, no request-answering yet */
    cache_insert(arp->sender_ip, arp->sender_mac);

    /* Log what we learned — the only real user-visible proof this
     * stage works, since there's no shell command to query the cache
     * yet. Manual decimal formatting since syslog_write takes a plain
     * string, no printf-style formatting available. */
    char msg[64];
    int p = 0;
    for (int i = 0; i < 4; i++) {
        int v = arp->sender_ip[i];
        if (v >= 100) msg[p++] = (char)('0' + v/100);
        if (v >= 10)  msg[p++] = (char)('0' + (v/10)%10);
        msg[p++] = (char)('0' + v%10);
        if (i < 3) msg[p++] = '.';
    }
    msg[p++] = ' '; msg[p++] = '-'; msg[p++] = '>'; msg[p++] = ' ';
    static const char hexd[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        msg[p++] = hexd[(arp->sender_mac[i] >> 4) & 0xF];
        msg[p++] = hexd[arp->sender_mac[i] & 0xF];
        if (i < 5) msg[p++] = ':';
    }
    msg[p] = 0;
    syslog_write("ARP", msg);
}

void net_poll(void) {
    static uint8_t frame[1600];
    uint16_t len;
    while ((len = rtl8139_recv(frame, sizeof(frame))) > 0) {
        if (len < sizeof(eth_hdr_t)) continue; /* too short to even have a valid Ethernet header */
        eth_hdr_t* eth = (eth_hdr_t*)frame;
        if (swap16(eth->ethertype) == ETHERTYPE_ARP) {
            if (len < sizeof(eth_hdr_t) + sizeof(arp_hdr_t)) continue;
            handle_arp((arp_hdr_t*)(frame + sizeof(eth_hdr_t)));
        }
        /* Non-ARP frames silently discarded — no IP layer yet to hand
         * them to. This is where EtherType 0x0800 (IPv4) dispatch will
         * be added in the next stage. */
    }
}
