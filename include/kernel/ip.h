#pragma once
#include <stdint.h>

/*
 * ip.c — IPv4 + ICMP (Phase 3.5 item #5, stage 3), built on stage 2's
 * ARP resolution and stage 1's raw RTL8139 frame I/O.
 *
 * No fragmentation, no IP options, no routing table (single flat
 * network assumed, matching QEMU user-mode's simple topology). Our own
 * IP is a static placeholder (10.0.2.15, QEMU user-net's default guest
 * address) — real DHCP is a future stage, not attempted here.
 *
 * ICMP is included in this stage (not deferred) because it's the
 * simplest real end-to-end proof that IP actually works round-trip —
 * same reasoning stage 2 used ARP resolution as its own proof rather
 * than just "an interrupt fired."
 */

#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17
#define IP_PROTO_TCP  6

void ip_init(const uint8_t our_ip[4]);

/* Called from net.c's Ethernet-level dispatch (EtherType 0x0800) with
 * the frame contents AFTER the 14-byte Ethernet header. */
void ip_handle_frame(const uint8_t* data, uint16_t len);

/* Sends payload as an IPv4 packet of the given protocol to dest_ip.
 * Returns 0 on success. Returns -1 if the destination's MAC isn't yet
 * in the ARP cache (this also triggers an arp_request() as a side
 * effect — caller should net_poll() a few times and retry, same
 * pattern as stage 2's self-test). payload_len must leave room for a
 * 20-byte IP header within a single Ethernet frame (i.e. <= 1480). */
int ip_send(const uint8_t dest_ip[4], uint8_t protocol, const void* payload, uint16_t payload_len);
