#pragma once
#include <stdint.h>

/*
 * rtl8139.c — Realtek RTL8139 NIC driver (Phase 3.5 item #5, stage 1).
 * Raw Ethernet frame send/receive only — no ARP/IP/UDP/TCP yet, those
 * are later stages built on top of this. Verified via a self-test that
 * sends a broadcast ARP request at boot and confirms an RX interrupt
 * fires in response (proof frames actually go out and come back).
 */

/* Returns 1 if a real RTL8139 was found and initialized, 0 otherwise
 * (e.g. QEMU booted without -device rtl8139, or genuinely no NIC). */
int rtl8139_init(void);

/* Copies the NIC's 6-byte MAC address into mac_out. */
void rtl8139_get_mac(uint8_t mac_out[6]);

/* Sends one raw Ethernet frame (caller includes the 14-byte Ethernet
 * header). Returns 0 on success, -1 if all 4 TX descriptors are
 * currently busy (caller should retry). len must be <= 1792. */
int rtl8139_send(const void* data, uint16_t len);

/* Non-blocking receive: if a frame is available, copies it into buf
 * (up to max_len bytes) and returns its actual length. Returns 0 if
 * nothing is available. */
uint16_t rtl8139_recv(void* buf, uint16_t max_len);
