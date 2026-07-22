#pragma once
#include <stdint.h>

/*
 * udp.c — UDP (Phase 3.5 item #5, stage 4), built on stage 3's IPv4.
 *
 * Deliberately minimal: no port-registration table or multi-listener
 * API. Real applications needing UDP don't exist yet, so a full
 * socket-style API would be speculative scope. Instead, exposes a
 * single "last received packet" capture point, sufficient for this
 * stage's self-test (a real DNS query to QEMU SLIRP's built-in proxy
 * at 10.0.2.3:53) and for whatever the next stage that actually needs
 * UDP turns out to require — expand then, not now.
 */

void udp_handle_packet(const uint8_t src_ip[4], const uint8_t* data, uint16_t len);

/* Sends a UDP datagram. Returns 0 on success, -1 on the same
 * "destination MAC not yet resolved" condition ip_send() returns —
 * caller should net_poll() and retry, same pattern as ip_send/arp. */
int udp_send(const uint8_t dest_ip[4], uint16_t dest_port, uint16_t src_port,
             const void* payload, uint16_t payload_len);

/* Set by udp_handle_packet() whenever any UDP datagram arrives — read
 * by the self-test, same pattern as icmp_echo_reply_seen. */
extern volatile int udp_packet_seen;
extern uint16_t udp_last_src_port;
