/*
 * YouOS - ATA PIO Mode Driver
 * Supports 28-bit LBA, primary bus, master drive.
 */

#include <kernel/ata.h>
#include <kernel/vga.h>
#include <stdint.h>

/* Port I/O helpers */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void insw(uint16_t port, void* addr, uint32_t count) {
    __asm__ volatile("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}
static inline void outsw(uint16_t port, const void* addr, uint32_t count) {
    __asm__ volatile("rep outsw" : "+S"(addr), "+c"(count) : "d"(port) : "memory");
}

/* 400ns delay by reading status 4 times */
static void ata_delay(void) {
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
}

/* Wait until BSY clears */
static int ata_wait_busy(void) {
    uint32_t timeout = 100000;
    while ((inb(ATA_PRIMARY_STATUS) & ATA_SR_BSY) && timeout--);
    return (timeout > 0) ? 0 : -1;
}

/* Wait until DRQ sets (data ready) */
static int ata_wait_drq(void) {
    uint32_t timeout = 100000;
    uint8_t status;
    while (timeout--) {
        status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if (status & ATA_SR_DRQ) return 0;
    }
    return -1;
}

int ata_init(void) {
    /* Reset drive */
    outb(ATA_PRIMARY_CTRL, 0x04);  /* SRST */
    ata_delay();
    outb(ATA_PRIMARY_CTRL, 0x00);  /* clear reset */
    ata_delay();

    /* Select master drive */
    outb(ATA_PRIMARY_DRIVE, 0xA0);
    ata_delay();

    /* Send IDENTIFY */
    outb(ATA_PRIMARY_SECCOUNT, 0);
    outb(ATA_PRIMARY_LBA_LO,   0);
    outb(ATA_PRIMARY_LBA_MID,  0);
    outb(ATA_PRIMARY_LBA_HI,   0);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    /* Check if drive exists */
    uint8_t status = 0;
    for (int i = 0; i < 1000; i++) {
        status = inb(ATA_PRIMARY_STATUS);
        if (status != 0) break;
        ata_delay();
    }
    if (status == 0) {
        vga_puts_color("  [!!] ATA: no drive detected\n", VGA_LIGHT_RED, VGA_BLACK);
        return 0;
    }

    /* Wait for response */
    if (ata_wait_busy() < 0) {
        vga_puts_color("  [!!] ATA: timeout waiting for identify\n", VGA_LIGHT_RED, VGA_BLACK);
        return 0;
    }

    /* Check LBA_MID/HI — if non-zero it's not ATA */
    if (inb(ATA_PRIMARY_LBA_MID) || inb(ATA_PRIMARY_LBA_HI)) {
        vga_puts_color("  [!!] ATA: not an ATA drive\n", VGA_LIGHT_RED, VGA_BLACK);
        return 0;
    }

    /* Read identify data (256 words) */
    if (ata_wait_drq() < 0) {
        vga_puts_color("  [!!] ATA: DRQ timeout\n", VGA_LIGHT_RED, VGA_BLACK);
        return 0;
    }

    uint16_t identify[256];
    for (int i = 0; i < 256; i++)
        identify[i] = inw(ATA_PRIMARY_DATA);

    /* Get drive size from words 60-61 (28-bit LBA sector count) */
    uint32_t sectors = ((uint32_t)identify[61] << 16) | identify[60];

    vga_puts_color("  [OK] ATA drive detected, ", VGA_LIGHT_GREEN, VGA_BLACK);

    /* Print size in MB */
    uint32_t mb = (sectors / 2) / 1024;
    char buf[16]; int i = 15; buf[i] = 0;
    if (mb == 0) { buf[--i] = '0'; }
    while (mb > 0) { buf[--i] = '0' + (mb % 10); mb /= 10; }
    vga_puts_color(&buf[i], VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts_color(" MB\n", VGA_LIGHT_GREEN, VGA_BLACK);

    return 1;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void* buf) {
    if (ata_wait_busy() < 0) return -1;

    /* Select master, send LBA bits 24-27 */
    outb(ATA_PRIMARY_DRIVE,    0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_ERROR,    0x00);
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA_LO,   (lba >>  0) & 0xFF);
    outb(ATA_PRIMARY_LBA_MID,  (lba >>  8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HI,   (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_COMMAND,  ATA_CMD_READ_PIO);

    uint16_t* ptr = (uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_busy() < 0) return -1;
        if (ata_wait_drq()  < 0) return -1;
        insw(ATA_PRIMARY_DATA, &ptr[s * 256], 256);
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void* buf) {
    if (ata_wait_busy() < 0) return -1;

    outb(ATA_PRIMARY_DRIVE,    0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_ERROR,    0x00);
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA_LO,   (lba >>  0) & 0xFF);
    outb(ATA_PRIMARY_LBA_MID,  (lba >>  8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HI,   (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_COMMAND,  ATA_CMD_WRITE_PIO);

    const uint16_t* ptr = (const uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_busy() < 0) return -1;
        if (ata_wait_drq()  < 0) return -1;
        outsw(ATA_PRIMARY_DATA, &ptr[s * 256], 256);
        /* Flush cache */
        outb(ATA_PRIMARY_COMMAND, 0xE7);
        ata_wait_busy();
    }
    return 0;
}
