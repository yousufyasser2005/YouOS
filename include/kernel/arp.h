#pragma once
#include <stdint.h>

/*
 * arp.c — Ethernet + ARP layer (Phase 3.5 item #5, stage 2), built on
 * top of the RTL8139 raw frame I/O from stage 1.
 *
 * Scope: parse incoming Ethernet frames by EtherType, dispatch ARP
 * replies into a small fixed-size IP->MAC cache, and provide
 * arp_request()/arp_resolve() so a future IP layer can resolve a
 * next-hop MAC before sending. No IP/UDP/TCP yet — this is purely the
 * link-layer address resolution piece those will depend on.
 */

#define ARP_CACHE_SIZE 8

/* Call once at boot, after rtl8139_init() succeeds. */
void arp_init(const uint8_t our_ip[4]);

/* Drain any pending received frames (via rtl8139_recv()) and process
 * them — call periodically, same pattern as uhci_poll(). Currently
 * only understands ARP (EtherType 0x0806); anything else is silently
 * discarded, since there's no IP layer yet to hand non-ARP frames to. */
void net_poll(void);

/* Sends a broadcast "who-has" ARP request for the given IPv4 address
 * (as 4 bytes, network byte order). */
void arp_request(const uint8_t ip[4]);

/* Looks up ip in the ARP cache. Returns 1 and fills mac_out on a hit,
 * 0 on a miss (caller may want to arp_request() and poll/retry). */
int arp_resolve(const uint8_t ip[4], uint8_t mac_out[6]);
