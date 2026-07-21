#include <kernel/pci.h>
#include <kernel/vga.h>

static inline void outl(uint16_t p, uint32_t v) {
    __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));
}
static inline uint32_t inl(uint16_t p) {
    uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v;
}

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                  | ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

static int pci_scan(int by_class, uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                     uint16_t vendor_id, uint16_t device_id, pci_device_t* out) {
    for (int b = 0; b < 8; b++) {
        for (int d = 0; d < 32; d++) {
            for (int f = 0; f < 8; f++) {
                uint32_t id_word = pci_config_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x00);
                uint16_t vid = id_word & 0xFFFF;
                if (vid == 0xFFFF) continue;
                uint16_t did = (id_word >> 16) & 0xFFFF;

                uint32_t class_word = pci_config_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x08);
                uint8_t cls = (class_word >> 24) & 0xFF;
                uint8_t sub = (class_word >> 16) & 0xFF;
                uint8_t pif = (class_word >> 8) & 0xFF;

                int match = by_class
                    ? (cls == class_code && sub == subclass && pif == prog_if)
                    : (vid == vendor_id && did == device_id);
                if (!match) continue;

                out->bus = (uint8_t)b; out->dev = (uint8_t)d; out->func = (uint8_t)f;
                out->vendor_id = vid; out->device_id = did;
                out->class_code = cls; out->subclass = sub; out->prog_if = pif;
                for (int barn = 0; barn < 6; barn++)
                    out->bar[barn] = pci_config_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, (uint8_t)(0x10 + barn * 4));
                return 1;
            }
        }
    }
    return 0;
}

int pci_find_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t* out) {
    return pci_scan(1, class_code, subclass, prog_if, 0, 0, out);
}

int pci_find_by_id(uint16_t vendor_id, uint16_t device_id, pci_device_t* out) {
    return pci_scan(0, 0, 0, 0, vendor_id, device_id, out);
}

void pci_enable_device(const pci_device_t* d) {
    uint32_t cmd = pci_config_read32(d->bus, d->dev, d->func, 0x04);
    cmd |= 0x07;
    pci_config_write32(d->bus, d->dev, d->func, 0x04, cmd);
}

uint32_t pci_bar_addr(const pci_device_t* d, int n) {
    if (n < 0 || n > 5) return 0;
    uint32_t bar = d->bar[n];
    if (bar & 1) return bar & 0xFFFFFFFCu;
    return bar & 0xFFFFFFF0u;
}

void pci_init(void) {
    int count = 0;
    for (int b = 0; b < 8; b++) {
        for (int d = 0; d < 32; d++) {
            for (int f = 0; f < 8; f++) {
                uint32_t id_word = pci_config_read32((uint8_t)b, (uint8_t)d, (uint8_t)f, 0x00);
                if ((id_word & 0xFFFF) == 0xFFFF) continue;
                count++;
            }
        }
    }
    if (count > 0) {
        vga_puts_color("  [OK] PCI bus scanned\n", VGA_LIGHT_GREEN, VGA_BLACK);
    } else {
        vga_puts_color("  [!!] PCI: no devices found\n", VGA_YELLOW, VGA_BLACK);
    }
}
