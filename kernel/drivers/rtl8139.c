/* RTL8139 NIC driver — Phase 3.5 item #5, stage 1 (raw frame I/O only). */
#include <kernel/rtl8139.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/irq.h>
#include <kernel/pic.h>
#include <kernel/idt.h>
#include <kernel/vga.h>

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl_(uint16_t p, uint32_t v){ __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t  inb(uint16_t p){ uint8_t  v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p){ uint16_t v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline uint32_t inl_(uint16_t p){ uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static void udelay(int n){ for(volatile int i=0;i<n*50;i++); }

/* ── RTL8139 register offsets (I/O space, BAR0) ─────────────────── */
#define R_MAC0    0x00 /* 6 bytes: station MAC address */
#define R_TSD0    0x10 /* 4x4 bytes: transmit status, descriptors 0-3 */
#define R_TSAD0   0x20 /* 4x4 bytes: transmit start address, descriptors 0-3 */
#define R_RBSTART 0x30 /* 4 bytes: receive buffer physical start address */
#define R_CR      0x37 /* 1 byte: command register */
#define R_CAPR    0x38 /* 2 bytes: current address of packet read */
#define R_IMR     0x3C /* 2 bytes: interrupt mask */
#define R_ISR     0x3E /* 2 bytes: interrupt status */
#define R_TCR     0x40 /* 4 bytes: transmit config */
#define R_RCR     0x44 /* 4 bytes: receive config */
#define R_CONFIG1 0x52 /* 1 byte: power/config */

#define CR_RST  0x10
#define CR_RE   0x08
#define CR_TE   0x04
#define CR_BUFE 0x01

#define ISR_ROK 0x01
#define ISR_TOK 0x04

#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

/* RX buffer: 8K ring + 16 bytes header slack + 1500 bytes so a frame
 * that wraps past the physical end still has room; rounded up to
 * whole 4K pages for pmm_alloc_pages. Total = 8192+16+1500 = 9708,
 * allocate 3 pages (12288) to be safely oversized. */
#define RX_BUF_PAGES 3
#define RX_BUF_SIZE  (RX_BUF_PAGES * 4096)

/* 4 TX descriptors, one page each — far more than a max 1514-byte
 * Ethernet frame needs, but page-granularity is what pmm gives us. */
#define TX_DESC_COUNT 4

static uint16_t io_base = 0;
static uint64_t rx_buf_phys = 0;
static uint16_t rx_offset = 0;
static uint64_t tx_buf_phys[TX_DESC_COUNT];
static int      tx_next = 0;
static uint8_t  mac[6];
volatile int rx_irq_seen = 0; /* set by IRQ handler, used by the self-test */

static void rtl8139_irq(registers_t* regs) {
    (void)regs;
    uint16_t status = inw(io_base + R_ISR);
    if (status & ISR_ROK) rx_irq_seen = 1;
    outw(io_base + R_ISR, status); /* ack by writing back what we read */
}

int rtl8139_init(void) {
    pci_device_t dev;
    if (!pci_find_by_id(RTL8139_VENDOR, RTL8139_DEVICE, &dev)) return 0;
    pci_enable_device(&dev);

    io_base = (uint16_t)pci_bar_addr(&dev, 0); /* BAR0 is I/O space on RTL8139 */
    if (!io_base) return 0;

    /* Power on (clear CONFIG1's sleep/power-down bits) */
    outb(io_base + R_CONFIG1, 0x00);

    /* Software reset, wait for the chip to clear CR_RST itself */
    outb(io_base + R_CR, CR_RST);
    int spins = 0;
    while ((inb(io_base + R_CR) & CR_RST) && spins < 1000) { udelay(100); spins++; }
    if (spins >= 1000) return 0; /* reset never completed — no real chip present */

    /* Read the burned-in MAC address */
    for (int i = 0; i < 6; i++) mac[i] = inb(io_base + R_MAC0 + i);

    /* Receive buffer: physically contiguous, DMA target for the NIC */
    rx_buf_phys = pmm_alloc_pages(RX_BUF_PAGES);
    if (!rx_buf_phys) return 0;
    outl_(io_base + R_RBSTART, (uint32_t)rx_buf_phys);
    rx_offset = 0;

    /* Transmit buffers: one page per descriptor */
    for (int i = 0; i < TX_DESC_COUNT; i++) {
        tx_buf_phys[i] = pmm_alloc_pages(1);
        if (!tx_buf_phys[i]) return 0;
    }
    tx_next = 0;

    /* IMR: enable RX-OK and TX-OK interrupts */
    outw(io_base + R_IMR, ISR_ROK | ISR_TOK);

    /* RCR: accept broadcast + multicast + physical-match + all-physical,
     * wrap bit set (simplifies ring handling — a frame near the buffer's
     * physical end can safely overrun into the following pages, which
     * is why RX_BUF_SIZE has 1500 bytes of slack past the nominal 8K). */
    outl_(io_base + R_RCR, 0x0000000Fu | (1u << 7));

    /* Enable receiver + transmitter */
    outb(io_base + R_CR, CR_RE | CR_TE);

    /* IRQ line comes from PCI config space (offset 0x3C, low byte) —
     * NOT hardcoded, since it's assigned by firmware and varies by
     * machine/bus topology, unlike legacy devices with fixed IRQs. */
    uint32_t irq_word = pci_config_read32(dev.bus, dev.dev, dev.func, 0x3C);
    uint8_t irq_line = (uint8_t)(irq_word & 0xFF);
    if (irq_line < 16) {
        irq_set_handler(irq_line, rtl8139_irq);
        pic_unmask(irq_line);
        /* If this landed on a slave-PIC line (IRQ 8-15, common for PCI
         * devices under QEMU's default routing — often 10 or 11), the
         * cascade line (IRQ 2) carrying ALL slave interrupts to the CPU
         * must also be unmasked, or nothing on 8-15 can ever fire
         * regardless of that IRQ's own mask bit. kernel_main.c's boot
         * sequence explicitly masks cascade + 9/10/11 as "unused" at
         * boot, which was true before any PCI IRQ device existed. */
        if (irq_line >= 8) pic_unmask(2);
    }

    vga_puts_color("  [OK] RTL8139 NIC initialized\n", VGA_LIGHT_GREEN, VGA_BLACK);
    return 1;
}

void rtl8139_get_mac(uint8_t mac_out[6]) {
    for (int i = 0; i < 6; i++) mac_out[i] = mac[i];
}

int rtl8139_send(const void* data, uint16_t len) {
    if (!io_base || len > 1792) return -1;

    int slot = tx_next;
    uint32_t status = inl_(io_base + R_TSD0 + slot * 4);
    /* Bit 13 (OWN) clear on read-back means the NIC still owns/is using
     * this descriptor from a prior send that hasn't completed yet. */
    if (!(status & (1u << 13)) && status != 0) return -1; /* still busy */

    uint8_t* txbuf = (uint8_t*)tx_buf_phys[slot];
    for (uint16_t i = 0; i < len; i++) txbuf[i] = ((const uint8_t*)data)[i];

    outl_(io_base + R_TSAD0 + slot * 4, (uint32_t)tx_buf_phys[slot]);
    outl_(io_base + R_TSD0 + slot * 4, (uint32_t)len); /* writing length starts transmission */

    tx_next = (tx_next + 1) % TX_DESC_COUNT;
    return 0;
}

uint16_t rtl8139_recv(void* buf, uint16_t max_len) {
    if (!io_base) return 0;
    if (inb(io_base + R_CR) & CR_BUFE) return 0; /* buffer empty, nothing to read */

    uint8_t* rx = (uint8_t*)rx_buf_phys;
    uint16_t frame_status = *(uint16_t*)(rx + rx_offset);
    uint16_t frame_len    = *(uint16_t*)(rx + rx_offset + 2);
    (void)frame_status;

    uint16_t copy_len = frame_len;
    if (copy_len > max_len) copy_len = max_len;
    for (uint16_t i = 0; i < copy_len; i++)
        ((uint8_t*)buf)[i] = rx[(rx_offset + 4 + i) % RX_BUF_SIZE];

    /* Advance past this frame's header+data, rounded up to a 4-byte
     * boundary (RTL8139 pads each RX entry to a dword boundary). */
    rx_offset = (uint16_t)((rx_offset + frame_len + 4 + 3) & ~3);
    if (rx_offset >= RX_BUF_SIZE) rx_offset -= RX_BUF_SIZE;

    /* CAPR must be set to (read offset - 16), per the chip's documented
     * quirk of expecting a 16-byte-before offset to avoid overwriting
     * its own in-flight write pointer. */
    outw(io_base + R_CAPR, (uint16_t)(rx_offset - 16));

    return frame_len;
}
