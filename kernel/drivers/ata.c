/*
 * YouOS - ATA PIO Mode Driver
 * Supports 28-bit and 48-bit LBA (PIO), primary bus, master drive.
 * LBAs below 2^28 always use the original 28-bit command path;
 * 48-bit commands are only issued for sectors at or beyond 128 GB.
 */

#include <kernel/ata.h>
#include <kernel/vga.h>
#include <stdint.h>

/* Drive facts filled in by ata_init() from IDENTIFY. */
static int      ata_lba48   = 0;
static uint8_t  ata_mult    = 0;   /* sectors per DRQ block for WRITE MULTIPLE; 0 = use plain WRITE SECTORS */
static uint64_t ata_sectors = 0;

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

/* Longer (~2ms) settle delay via repeated port reads. This runs before
 * the timer/scheduler is initialized during boot, so a real time-based
 * sleep isn't available yet -- this approximates one instead. Needed
 * because a real drive/controller can take noticeably longer than a
 * single ata_delay() (~400ns) to settle after a software reset before
 * status reads become reliable; the previous single short delay left
 * too little margin, which is why detection was flaky/timing-dependent
 * under QEMU (see prior session's handoff doc -- logged as a known,
 * low-priority backlog item, not a functional problem, since real PIO
 * reads still worked independently and reliably regardless). */
static void ata_delay_long(void) {
    for (int i = 0; i < 5000; i++) ata_delay();
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
    /* Give the drive real time to settle after the reset (see
     * ata_delay_long()'s comment) before trusting any status read, and
     * explicitly wait for BSY to negate rather than assuming a fixed
     * delay is always enough. */
    ata_delay_long();
    ata_wait_busy();

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

    /* Check if drive exists. Given more patience than before (10x the
     * iterations) so a slow-to-respond but genuinely present drive
     * isn't misreported as absent under slower emulation timing. */
    uint8_t status = 0;
    for (int i = 0; i < 10000; i++) {
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

    /* Capacity: words 60-61 are the 28-bit sector count (clamped to
     * 0x0FFFFFFF on bigger drives); word 83 bit 10 says LBA48 is
     * supported, in which case words 100-103 hold the true 48-bit count. */
    uint32_t sectors28 = ((uint32_t)identify[61] << 16) | identify[60];
    uint64_t sectors   = sectors28;
    ata_lba48 = (identify[83] & (1u << 10)) != 0;
    if (ata_lba48) {
        uint64_t s48 = ((uint64_t)identify[103] << 48) | ((uint64_t)identify[102] << 32) |
                       ((uint64_t)identify[101] << 16) |  (uint64_t)identify[100];
        if (s48 > sectors) sectors = s48;
    }
    ata_sectors = sectors;

    vga_puts_color("  [OK] ATA drive detected, ", VGA_LIGHT_GREEN, VGA_BLACK);

    /* Print size in MB, or GB once it's big enough that MB is unwieldy */
    uint64_t mb = (sectors / 2) / 1024;
    const char* unit = " MB\n";
    if (mb >= 100000) { mb /= 1024; unit = " GB\n"; }
    char buf[24]; int i = 23; buf[i] = 0;
    if (mb == 0) { buf[--i] = '0'; }
    while (mb > 0) { buf[--i] = '0' + (mb % 10); mb /= 10; }
    vga_puts_color(&buf[i], VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts_color(unit, VGA_LIGHT_GREEN, VGA_BLACK);
    if (ata_lba48) vga_puts_color("       (LBA48 supported)\n", VGA_LIGHT_GREEN, VGA_BLACK);

    /* IDENTIFY word 47, low byte: max sectors per DRQ block for READ/WRITE MULTIPLE. */
    {
        uint8_t maxm = (uint8_t)(identify[47] & 0xFF);
        int got = (maxm >= 2) ? ata_enable_multiple(maxm) : -1;
        if (got >= 2) {
            char nb[8]; int k = 7; nb[k] = 0; int v = got;
            while (v > 0) { nb[--k] = (char)('0' + v % 10); v /= 10; }
            vga_puts_color("       multiple-sector writes: ", VGA_LIGHT_GREEN, VGA_BLACK);
            vga_puts_color(&nb[k], VGA_LIGHT_GREEN, VGA_BLACK);
            vga_puts_color(" sectors/block (self-test passed)\n", VGA_LIGHT_GREEN, VGA_BLACK);
        } else {
            vga_puts_color("       multiple-sector writes: not used\n", VGA_LIGHT_GREEN, VGA_BLACK);
        }
    }

    return 1;
}

uint64_t ata_capacity_sectors(void) { return ata_sectors; }

/* Last sector number addressable with the 28-bit commands, plus one. */
#define ATA_LBA28_LIMIT 0x10000000ull

/* Program the task-file registers for a transfer of `count` sectors at
 * `lba` and issue the command. Chooses 28-bit or 48-bit form. Returns 0,
 * -1 if the drive is busy, -2 if the LBA needs LBA48 and the drive lacks it. */
static int ata_issue(uint64_t lba, uint8_t count, int write, int multi, int* used48) {
    if (ata_wait_busy() < 0) return -1;
    if (lba + count <= ATA_LBA28_LIMIT) {
        *used48 = 0;
        outb(ATA_PRIMARY_DRIVE,    0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_PRIMARY_ERROR,    0x00);
        outb(ATA_PRIMARY_SECCOUNT, count);
        outb(ATA_PRIMARY_LBA_LO,   (lba >>  0) & 0xFF);
        outb(ATA_PRIMARY_LBA_MID,  (lba >>  8) & 0xFF);
        outb(ATA_PRIMARY_LBA_HI,   (lba >> 16) & 0xFF);
        outb(ATA_PRIMARY_COMMAND,  write ? (multi ? ATA_CMD_WRITE_MULTIPLE : ATA_CMD_WRITE_PIO) : ATA_CMD_READ_PIO);
        return 0;
    }
    if (!ata_lba48 || lba + count > ata_sectors) return -2;
    *used48 = 1;
    /* LBA48: write the HIGH bytes first, then the LOW bytes, to the same
     * registers (the controller latches them as a two-deep FIFO). */
    outb(ATA_PRIMARY_DRIVE,    0x40);                 /* LBA mode, master */
    outb(ATA_PRIMARY_ERROR,    0x00);
    outb(ATA_PRIMARY_SECCOUNT, 0x00);                 /* count bits 8-15  */
    outb(ATA_PRIMARY_LBA_LO,   (lba >> 24) & 0xFF);
    outb(ATA_PRIMARY_LBA_MID,  (lba >> 32) & 0xFF);
    outb(ATA_PRIMARY_LBA_HI,   (lba >> 40) & 0xFF);
    outb(ATA_PRIMARY_SECCOUNT, count);                /* count bits 0-7   */
    outb(ATA_PRIMARY_LBA_LO,   (lba >>  0) & 0xFF);
    outb(ATA_PRIMARY_LBA_MID,  (lba >>  8) & 0xFF);
    outb(ATA_PRIMARY_LBA_HI,   (lba >> 16) & 0xFF);
    outb(ATA_PRIMARY_COMMAND,  write ? (multi ? ATA_CMD_WRITE_MULTIPLE_EXT : ATA_CMD_WRITE_PIO_EXT) : ATA_CMD_READ_PIO_EXT);
    return 0;
}

int ata_read_sectors(uint64_t lba, uint8_t count, void* buf) {
    int used48;
    if (ata_issue(lba, count, 0, 0, &used48) != 0) return -1;

    uint16_t* ptr = (uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_busy() < 0) return -1;
        if (ata_wait_drq()  < 0) return -1;
        insw(ATA_PRIMARY_DATA, &ptr[s * 256], 256);
    }
    return 0;
}

/* Wait for the last data to reach the drive's cache, then flush it so the data is
 * durable when the caller returns. 48-bit writes use FLUSH CACHE EXT (0xEA). */
static int ata_finish_write(int used48) {
    if (ata_wait_busy() < 0) return -1;
    outb(ATA_PRIMARY_COMMAND, used48 ? ATA_CMD_FLUSH_EXT : ATA_CMD_FLUSH);
    ata_delay();
    if (ata_wait_busy() < 0) return -1;
    return 0;
}

/* Plain WRITE SECTORS: one DRQ block per sector. Works on every drive, but an
 * emulated/virtual drive may turn each sector into a separate synchronous host write. */
static int ata_write_single(uint64_t lba, uint8_t count, const void* buf) {
    int used48;
    if (ata_issue(lba, count, 1, 0, &used48) != 0) return -1;

    const uint16_t* ptr = (const uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (ata_wait_busy() < 0) return -1;
        if (ata_wait_drq()  < 0) return -1;
        outsw(ATA_PRIMARY_DATA, &ptr[s * 256], 256);
        /* ATA requires ~400ns after the last data word before the status
         * register is trustworthy: until the drive raises BSY, a read
         * still shows the PREVIOUS (not-busy, DRQ-set) status, and the
         * next iteration's waits would return at once and push the next
         * sector early. History: this loop used to flush the drive cache
         * after every sector, partly for that side effect (fixed in the
         * 2026-08-28 investigation: runtime writes silently not
         * surviving a reboot). A flush in the middle of a multi-sector
         * WRITE is itself a protocol violation on real drives and cost 8
         * flushes per 4KB block, so the settle delay is now explicit and
         * the cache is flushed ONCE, after the last sector. */
        ata_delay();
    }
    return ata_finish_write(used48);
}

/* WRITE MULTIPLE: the drive takes `ata_mult` sectors per DRQ block instead of one.
 * Same data, same result -- but a virtual drive can then write the whole block to
 * its backing file in ONE host operation instead of one synchronous write per
 * sector (measured: ~4 ms per sector under QEMU's cache=writethrough). */
static int ata_write_multi(uint64_t lba, uint8_t count, const void* buf) {
    int used48;
    if (ata_issue(lba, count, 1, 1, &used48) != 0) return -1;

    const uint16_t* ptr = (const uint16_t*)buf;
    uint8_t left = count;
    while (left) {
        uint8_t n = left < ata_mult ? left : ata_mult;      /* the last block may be shorter */
        if (ata_wait_busy() < 0) return -1;
        if (ata_wait_drq()  < 0) return -1;
        outsw(ATA_PRIMARY_DATA, ptr, (uint32_t)n * 256);
        ptr  += (uint32_t)n * 256;
        left  = (uint8_t)(left - n);
        ata_delay();                                          /* same settle rule as above */
    }
    return ata_finish_write(used48);
}

int ata_write_sectors(uint64_t lba, uint8_t count, const void* buf) {
    if (ata_mult > 1 && count > 1) {
        if (ata_write_multi(lba, count, buf) == 0) return 0;
        /* The drive refused or mishandled it: stop using multiple mode for good and
         * redo the whole write the old way (writing the same data again is harmless). */
        ata_mult = 0;
    }
    return ata_write_single(lba, count, buf);
}

/* Turn on multiple-sector writes if the drive supports them, then PROVE they work
 * before anything depends on them. The proof needs somewhere to write test data, so
 * it uses 16 sectors near the END of the disk that currently read as all zeros (so
 * nothing lives there); it writes a distinct pattern with the new command, reads it
 * back and compares, then puts the zeros back. A drive that loses or mangles part of
 * a multi-sector write is caught because the pattern differs from what was there.
 * (Rewriting a sector with its OWN contents would not catch a dropped write.)
 * If no zeroed region is found, or anything fails, plain writes stay in use.
 * `max` is the drive's advertised sectors-per-block (IDENTIFY word 47, low byte). */
int ata_enable_multiple(uint8_t max) {
    static uint8_t zeros[16 * 512], pat[16 * 512], back[16 * 512];
    uint8_t n = 16;
    while (n > max) n >>= 1;                    /* SET MULTIPLE wants a power of two <= max */
    if (n < 2) return -1;
    if (ata_sectors < 4096) return -1;          /* too small to have a safe scratch area */

    /* find a scratch region that is all zeros */
    uint64_t lba = 0; int found = 0;
    for (int t = 0; t < 4 && !found; t++) {
        uint64_t cand = ((ata_sectors - 128) & ~15ull) - (uint64_t)t * 128;
        if (ata_read_sectors(cand, 16, back) != 0) return -1;
        int allzero = 1;
        for (int i = 0; i < 16 * 512; i++) if (back[i]) { allzero = 0; break; }
        if (allzero) { lba = cand; found = 1; }
    }
    if (!found) return -1;

    /* SET MULTIPLE MODE */
    if (ata_wait_busy() < 0) return -1;
    outb(ATA_PRIMARY_DRIVE,    0xE0);
    outb(ATA_PRIMARY_SECCOUNT, n);
    outb(ATA_PRIMARY_COMMAND,  ATA_CMD_SET_MULTIPLE);
    ata_delay();
    if (ata_wait_busy() < 0) return -1;
    if (inb(ATA_PRIMARY_STATUS) & (ATA_SR_ERR | ATA_SR_DF)) return -1;

    for (int i = 0; i < 16 * 512; i++) pat[i] = (uint8_t)((i * 13 + 5) | 1);   /* never zero */
    ata_mult = n;
    int ok = (ata_write_multi(lba, 16, pat) == 0) && (ata_read_sectors(lba, 16, back) == 0);
    if (ok) for (int i = 0; i < 16 * 512; i++) if (pat[i] != back[i]) { ok = 0; break; }

    /* put the zeros back with the plain command, whatever happened */
    int restored = (ata_write_single(lba, 16, zeros) == 0);
    if (!ok || !restored) { ata_mult = 0; return -1; }
    return (int)n;
}

uint8_t ata_multiple_sectors(void) { return ata_mult; }
