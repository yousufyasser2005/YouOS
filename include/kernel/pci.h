#pragma once
#include <stdint.h>

/*
 * pci.c — minimal PCI configuration-space enumeration.
 *
 * Extracted from what was previously a private scan-in-place inside
 * uhci.c (find_uhci()) — that logic worked fine for one device but
 * doesn't generalize. This gives every PCI-attached driver (UHCI now;
 * RTL8139 for Phase 3.5 item #5; any future AHCI/xHCI work for item #3)
 * a single shared way to find its device, without re-deriving the
 * config-space address mechanics each time.
 *
 * Scans bus 0-7, device 0-31, function 0-7 (same bounds as the
 * original uhci.c scan) via the legacy 0xCF8/0xCFC I/O-port mechanism.
 * This does NOT walk PCI-to-PCI bridges to discover secondary buses
 * beyond that fixed range, and does NOT support MMCONFIG (memory-mapped
 * config space) — both are real-hardware gaps flagged for item #3, not
 * addressed here since QEMU's virtual topology fits well within these
 * bounds.
 */

typedef struct {
    uint8_t  bus, dev, func;
    uint16_t vendor_id, device_id;
    uint8_t  class_code, subclass, prog_if;
    uint32_t bar[6];
} pci_device_t;

void pci_init(void);
int pci_find_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* out);
int pci_find_by_id(uint16_t vendor_id, uint16_t device_id, pci_device_t* out);
void pci_enable_device(const pci_device_t* d);
uint32_t pci_bar_addr(const pci_device_t* d, int n);
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
