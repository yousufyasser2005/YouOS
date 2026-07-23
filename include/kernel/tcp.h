#pragma once
#include <stdint.h>

/*
 * tcp.c — TCP (Phase 3.5 item #5, stage 5a: handshake + graceful close
 * only, no data transfer yet — that's stage 5b).
 *
 * Single static connection, client-side (active open) only — no
 * listening/server support, no connection table, matches the "one
 * thing at a time" shape of arp.c/ip.c/udp.c. No retransmission timer
 * yet either (a real gap for anything beyond a controlled self-test
 * against a reliable host — flagged, not fixed, in this stage).
 */

typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_TIME_WAIT,
    TCP_LAST_ACK, /* passive close: remote sent FIN first (e.g. HTTP
                     server closing after its response) — we ACK it,
                     send our own FIN, and wait for the final ACK. */
} tcp_state_t;

void tcp_init(void);

/* Starts an active open (sends SYN). Non-blocking — call tcp_poll()
 * (or just net_poll(), which calls it) and check tcp_get_state() to
 * observe progress, same pattern as arp_resolve()/ip_send(). Returns
 * -1 if a connection is already in progress (only one at a time). */
int tcp_connect(const uint8_t dest_ip[4], uint16_t dest_port);

/* Starts a graceful close (sends FIN) from ESTABLISHED. No-op if not
 * currently ESTABLISHED. */
void tcp_close(void);

/* Sends data on the established connection. Returns 0 on success, -1
 * if not currently ESTABLISHED. Does not wait for the remote's ACK —
 * no retransmission in this stage; fine for a controlled self-test
 * against a reliable host, a real gap for production use. */
int tcp_send(const void* data, uint16_t len);

/* Non-blocking: copies any newly-received data into buf (up to
 * max_len), returns the number of bytes copied, 0 if nothing new. */
uint16_t tcp_recv(void* buf, uint16_t max_len);

tcp_state_t tcp_get_state(void);

/* Called from ip.c's protocol dispatch for IP_PROTO_TCP frames. */
void tcp_handle_segment(const uint8_t src_ip[4], const uint8_t* data, uint16_t len);
