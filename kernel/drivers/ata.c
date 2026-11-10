/*
 * YouOS - ATA PIO Mode Driver
 * Supports 28-bit and 48-bit LBA (PIO), primary bus, master drive.
 * LBAs below 2^28 always use the original 28-bit command path;
 * 48-bit commands are only issued for sectors at or beyond 128 GB.
 */

#include <kernel/ata.h>
#include <kernel/vga.h>
#include <kernel/pci.h>
#include <kernel/vmm.h>
#include <kernel/pic.h>
#include <kernel/process.h>
#include <stdint.h>

/* Drive facts filled in by ata_init() from IDENTIFY. */
static int      ata_lba48   = 0;
static uint8_t  ata_mult    = 0;   /* sectors per DRQ block for WRITE MULTIPLE; 0 = use plain WRITE SECTORS */
static uint64_t ata_sectors = 0;
static uint16_t ata_bm      = 0;   /* bus-master IDE I/O base (PCI BAR4); 0 = no DMA controller */
static int      ata_dma_ok  = 0;   /* 1 once ata_enable_dma()'s self-test has passed */
static int      ata_dma_read_ok = 0;   /* 1 once the DMA READ self-test has passed too */

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
static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
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

    /* Bus-master DMA writes (needs the multiple-sector/PIO paths to be settled first: it uses
     * them to put its scratch sectors back). */
    if (ata_enable_dma() == 1)
        vga_puts_color("       DMA writes: enabled (self-test passed)\n", VGA_LIGHT_GREEN, VGA_BLACK);
    else
        vga_puts_color("       DMA writes: not used\n", VGA_LIGHT_GREEN, VGA_BLACK);

    return 1;
}

uint64_t ata_capacity_sectors(void) { return ata_sectors; }

/* Last sector number addressable with the 28-bit commands, plus one. */
#define ATA_LBA28_LIMIT 0x10000000ull

/* Program the task-file registers for a transfer of `count` sectors at
 * `lba` and issue the command. Chooses 28-bit or 48-bit form. Returns 0,
 * -1 if the drive is busy, -2 if the LBA needs LBA48 and the drive lacks it. */
/* mode: 0 = plain PIO, 1 = WRITE MULTIPLE (PIO), 2 = DMA. */
static int ata_issue(uint64_t lba, uint8_t count, int write, int mode, int* used48) {
    if (ata_wait_busy() < 0) return -1;
    if (lba + count <= ATA_LBA28_LIMIT) {
        *used48 = 0;
        outb(ATA_PRIMARY_DRIVE,    0xE0 | ((lba >> 24) & 0x0F));
        outb(ATA_PRIMARY_ERROR,    0x00);
        outb(ATA_PRIMARY_SECCOUNT, count);
        outb(ATA_PRIMARY_LBA_LO,   (lba >>  0) & 0xFF);
        outb(ATA_PRIMARY_LBA_MID,  (lba >>  8) & 0xFF);
        outb(ATA_PRIMARY_LBA_HI,   (lba >> 16) & 0xFF);
        outb(ATA_PRIMARY_COMMAND,  write ? (mode == 2 ? ATA_CMD_WRITE_DMA : mode == 1 ? ATA_CMD_WRITE_MULTIPLE : ATA_CMD_WRITE_PIO) : (mode == 2 ? ATA_CMD_READ_DMA : ATA_CMD_READ_PIO));
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
    outb(ATA_PRIMARY_COMMAND,  write ? (mode == 2 ? ATA_CMD_WRITE_DMA_EXT : mode == 1 ? ATA_CMD_WRITE_MULTIPLE_EXT : ATA_CMD_WRITE_PIO_EXT) : (mode == 2 ? ATA_CMD_READ_DMA_EXT : ATA_CMD_READ_PIO_EXT));
    return 0;
}

/* ---- timing counters (diagnostics; read via ata_stats_take) ---- */
ata_stats_t ata_stats;
static inline uint64_t ata_tsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
void ata_stats_take(ata_stats_t* out) {
    *out = ata_stats;
    ata_stats_t z = {0};
    ata_stats = z;
}

static int ata_read_sectors_inner(uint64_t lba, uint8_t count, void* buf);
/* ---- interrupt-driven completion ----------------------------------------------------------
 * While a DMA transfer or a cache flush is in progress the CPU has nothing to do, so the waiting
 * process goes to sleep (other processes run) and the drive's interrupt (IRQ14) wakes it. The
 * drive's interrupt line is only enabled (nIEN=0) while we are about to wait for it; all polled
 * commands keep it off. IRQ mode is switched on by ata_irq_init() only after it has been SEEN to
 * work; until then, and whenever it is off, waits poll exactly as before. */
static int ata_irq_mode = 0;
static volatile uint32_t ata_irq_count = 0;
void ata_irq_handler(void) {                     /* called from the IRQ14 stub, interrupts off */
    (void)inb(ATA_PRIMARY_STATUS);               /* reading Status acknowledges the drive's interrupt */
    ata_irq_count++;
    sched_wake((void*)&ata_irq_count);
}
static inline void ata_irq_arm(void)    { if (ata_irq_mode) outb(ATA_PRIMARY_CTRL, 0x00); }
static inline void ata_irq_disarm(void) { if (ata_irq_mode) outb(ATA_PRIMARY_CTRL, 0x02); }
/* Sleep until the drive interrupts, but never longer than 2 ticks (20 ms) at a time, so a lost
 * interrupt can only delay a transfer, never hang it. */
static inline void ata_sleep_for_irq(void) { sched_block((void*)&ata_irq_count, 2); }
/* BSY wait that sleeps instead of spinning when IRQ mode is on (used for the cache flush). */
extern uint64_t ata_dma_timeout_cycles;
static inline uint64_t ata_dma_timeout_cycles_hint(void) { return ata_dma_timeout_cycles; }
static int ata_wait_busy_sleep(void) {
    if (!ata_irq_mode) return ata_wait_busy();
    uint64_t t0 = ata_tsc();
    for (;;) {
        if (!(inb(ATA_PRIMARY_CTRL) & ATA_SR_BSY)) return 0;      /* alternate status: does not touch the interrupt */
        if (ata_tsc() - t0 > ata_dma_timeout_cycles_hint()) return -1;
        ata_sleep_for_irq();
    }
}

static int ata_read_dma(uint64_t lba, uint8_t count, void* buf);
static void ata_log_read_dma_failed(uint64_t lba, uint8_t count);
int ata_read_sectors(uint64_t lba, uint8_t count, void* buf) {
    uint64_t t0 = ata_tsc();
    int r;
    if (ata_dma_ok && ata_dma_read_ok && count >= 4 && ata_read_dma(lba, count, buf) == 0) {
        r = 0; ata_stats.rd_dma_cmds++;
    } else {
        if (ata_dma_ok && ata_dma_read_ok && count >= 4) {   /* DMA read failed: no more DMA reads, redo this one by PIO */
            ata_dma_read_ok = 0;
            ata_log_read_dma_failed(lba, count);
        }
        r = ata_read_sectors_inner(lba, count, buf);
    }
    ata_stats.rd_cmds++; ata_stats.rd_sectors += count; ata_stats.rd_cycles += ata_tsc() - t0;
    return r;
}
static int ata_read_sectors_inner(uint64_t lba, uint8_t count, void* buf) {
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
static int ata_defer_flush = 0;          /* set while ata_write_sectors_nf() runs */
static int ata_flush_pending = 0, ata_flush_pending48 = 0;
static int ata_finish_write(int used48) {
    if (ata_wait_busy() < 0) return -1;
    if (ata_defer_flush) { ata_flush_pending = 1; ata_flush_pending48 = used48; return 0; }
    uint64_t t0 = ata_tsc();
    ata_irq_arm();
    outb(ATA_PRIMARY_COMMAND, used48 ? ATA_CMD_FLUSH_EXT : ATA_CMD_FLUSH);
    ata_delay();
    int r = ata_wait_busy_sleep() < 0 ? -1 : 0;
    ata_irq_disarm();
    ata_stats.fl_cmds++; ata_stats.fl_cycles += ata_tsc() - t0;
    ata_flush_pending = 0;                              /* this flush covers everything written before it */
    return r;
}
int ata_flush_now(void) {
    if (!ata_flush_pending) return 0;
    int saved = ata_defer_flush; ata_defer_flush = 0;
    int r = ata_finish_write(ata_flush_pending48);
    ata_defer_flush = saved;
    return r;
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

/* ---- bus-master DMA ------------------------------------------------------
 * The IDE controller reads the data straight from memory, described by a PRD
 * (physical region descriptor) table: {u32 physical address, u16 byte count,
 * u16 flags; bit 15 = last entry}. A whole command (up to 255 sectors) is then
 * ONE operation for the drive: under a virtual disk that is one host write
 * rather than one per 8KB (WRITE MULTIPLE) or one per sector (WRITE SECTORS).
 *
 * Data is copied into a private bounce buffer first, so callers may pass any
 * memory (kernel statics, stack, ...). Each 64KB piece of the buffer sits on a
 * 64KB boundary, as the PRD rules require. The padding around the buffer and
 * the table is only there to catch a misdirected transfer: the boot self-test
 * fills it with a pattern and checks that nothing touched it. */
#define ATA_DMA_BYTES 131072
static struct {
    uint8_t  pad_lo[65536];
    uint8_t  buf[ATA_DMA_BYTES];
    uint8_t  pad_hi[65536];
    uint32_t prd[8];                     /* up to 4 entries of {address, count|flags} */
    uint8_t  pad_prd[4096];
} ata_dma __attribute__((aligned(65536)));

static void ata_log_num(char* b, int* k, uint64_t v);
static void ata_log_str(char* b, int* k, const char* a);
/* How long a DMA command may take before we give up (TSC cycles; ~13 s at 2.3 GHz).
 * A variable so the host tests can shorten it. */
uint64_t ata_dma_timeout_cycles = 5000000000ULL;   /* ~2 s; a healthy command finishes in well under a millisecond */
static pci_device_t ata_pci;                       /* the IDE controller, remembered by ata_enable_dma() */
static int ata_pci_ok = 0;
static uint32_t ata_dma_pcicmd = 0, ata_dma_bme_fixed = 0;   /* PCI command word at the last command; times bus mastering had to be re-enabled */
static uint8_t  ata_dma_pre = 0, ata_dma_cmdb = 0;           /* bus-master status before the command, command reg before START */
static uint32_t ata_dma_prdrb = 0;                           /* PRD address as read back from the controller */
static uint64_t ata_dma_ph0 = 0, ata_dma_php = 0;            /* physical addresses used for the data and the PRD table */
static uint64_t ata_phys_buf = 0, ata_phys_prd = 0;           /* physical addresses verified by the boot self-test */
static int ata_phys_warned = 0;
static int ata_dma_recoveries = 0;                           /* stalls recovered by reset-and-retry (max 2 per boot) */
static uint64_t ata_dma_waited = 0, ata_dma_polls = 0;   /* last DMA command: cycles waited, status polls */
static uint8_t  ata_dma_dst = 0, ata_dma_err = 0;         /* drive status / error register after a failure */
static int ata_dma_logged = 0;                            /* DMA commands reported on serial so far */
static int ata_dma_why = 0;       /* why the last DMA write failed: 1 issue, 2 timeout, 3 ctrl error, 4 busy, 5 drive error, 6 flush */
static uint8_t ata_dma_bst = 0;  /* bus-master status at that moment */
static int ata_dma_attempt(uint64_t lba, uint8_t count, const void* buf) {
    uint32_t bytes = (uint32_t)count * 512;
    const uint8_t* src = (const uint8_t*)buf;
    for (uint32_t i = 0; i < bytes; i++) ata_dma.buf[i] = src[i];

    /* Bus mastering must be on in the controller's PCI command register, or the controller
     * cannot read the data. Check before every command and switch it back on if something
     * cleared it. */
    if (ata_pci_ok) {
        ata_dma_pcicmd = pci_config_read32(ata_pci.bus, ata_pci.dev, ata_pci.func, 0x04) & 0xFFFF;
        if (!(ata_dma_pcicmd & 0x04)) {
            pci_enable_device(&ata_pci);
            ata_dma_bme_fixed++;
            char b[100]; int k = 0;
            ata_log_str(b, &k, "[ata] PCI bus mastering was OFF before a DMA command (cmd=");
            ata_log_num(b, &k, ata_dma_pcicmd); ata_log_str(b, &k, "); switched it back on\n");
            vga_serial_write(b, (uint64_t)k);
        }
    }

    /* Physical addresses of the bounce buffer and the PRD table. These are static kernel data, so
     * they never move: use the values the boot self-test proved correct. Looking them up again
     * here is not safe -- at run time the kernel's table walk was seen returning 0 for them, which
     * made the controller wait on a PRD table at physical address 0 forever. */
    uint64_t ph0 = ata_phys_buf, php = ata_phys_prd;
    if (!ph0 || !php) {                       /* never verified (cannot happen after a real enable): look them up */
        ph0 = vmm_get_phys(&kernel_as, (uint64_t)(uintptr_t)ata_dma.buf);
        php = vmm_get_phys(&kernel_as, (uint64_t)(uintptr_t)ata_dma.prd);
    } else if (!ata_phys_warned) {
        uint64_t l0 = vmm_get_phys(&kernel_as, (uint64_t)(uintptr_t)ata_dma.buf);
        uint64_t l1 = vmm_get_phys(&kernel_as, (uint64_t)(uintptr_t)ata_dma.prd);
        if (l0 != ph0 || l1 != php) {
            ata_phys_warned = 1;
            uint64_t w[4]; vmm_walk_debug(&kernel_as, (uint64_t)(uintptr_t)ata_dma.buf, w);
            char b[300]; int k = 0;
            ata_log_str(b, &k, "[ata] note: live address lookup now gives data="); ata_log_num(b, &k, l0);
            ata_log_str(b, &k, " prd="); ata_log_num(b, &k, l1);
            ata_log_str(b, &k, " but the boot self-test verified data="); ata_log_num(b, &k, ph0);
            ata_log_str(b, &k, " prd="); ata_log_num(b, &k, php);
            ata_log_str(b, &k, "; using the verified ones. walk(pml4,pdp,pd,pt)=");
            ata_log_num(b, &k, w[0]); b[k++] = ','; ata_log_num(b, &k, w[1]); b[k++] = ',';
            ata_log_num(b, &k, w[2]); b[k++] = ','; ata_log_num(b, &k, w[3]); b[k++] = '\n';
            vga_serial_write(b, (uint64_t)k);
        }
    }
    uint32_t first = bytes > 65536 ? 65536 : bytes;
    ata_dma_ph0 = ph0; ata_dma_php = php;
    ata_dma.prd[0] = (uint32_t)ph0;
    ata_dma.prd[1] = (first == 65536 ? 0u : first) | (bytes <= 65536 ? 0x80000000u : 0u);
    if (bytes > 65536) {
        ata_dma.prd[2] = (uint32_t)(ph0 + 65536);
        ata_dma.prd[3] = (bytes - 65536) | 0x80000000u;
    }

    outl((uint16_t)(ata_bm + 4), (uint32_t)php);                          /* PRD table address */
    outb((uint16_t)(ata_bm + 2), (uint8_t)(inb((uint16_t)(ata_bm + 2)) | 0x06)); /* clear interrupt + error (write 1) */
    outb((uint16_t)(ata_bm + 0), 0x00);                                   /* direction: memory -> drive; stopped */
    int used48;
    ata_dma_pre = inb((uint16_t)(ata_bm + 2));
    ata_irq_arm();
    if (ata_issue(lba, count, 1, 2, &used48) != 0) { ata_irq_disarm(); ata_dma_why = 1; return -1; }
    ata_dma_cmdb = inb((uint16_t)(ata_bm + 0));
    ata_dma_prdrb = inl((uint16_t)(ata_bm + 4));
    outb((uint16_t)(ata_bm + 0), 0x01);                                   /* start */

    /* Wait for the controller. Between two status reads the CPU spins on the time-stamp
     * counter for ~10 microseconds instead of hammering the port: under QEMU every port read
     * is a VM exit that takes the emulator's global lock, and a tight read loop can keep
     * the emulator's own thread (the one that completes the disk write) from running. */
    int done = 0;
    uint64_t wt0 = ata_tsc(), polls = 0;
    for (;;) {
        uint8_t st = inb((uint16_t)(ata_bm + 2));
        polls++;
        if (st & 0x02) break;                                             /* controller reports an error */
        if (!(st & 0x01)) { done = 1; break; }                            /* transfer no longer active */
        uint64_t now = ata_tsc();
        if (now - wt0 > ata_dma_timeout_cycles) break;                    /* give up */
        if (ata_irq_mode) { ata_sleep_for_irq(); continue; }               /* sleep until the drive interrupts */
        uint64_t until = now + 20000;
        while (ata_tsc() < until) __asm__ volatile("pause");
    }
    ata_irq_disarm();
    ata_dma_waited = ata_tsc() - wt0; ata_dma_polls = polls;
    ata_dma_dst = inb(ATA_PRIMARY_STATUS); ata_dma_err = inb(ATA_PRIMARY_ERROR);
    outb((uint16_t)(ata_bm + 0), 0x00);                                   /* stop */
    uint8_t bst = inb((uint16_t)(ata_bm + 2));
    outb((uint16_t)(ata_bm + 2), (uint8_t)(bst | 0x06));
    ata_dma_bst = bst;
    if (bst & 0x02) { ata_dma_why = 3; return -1; }           /* controller error bit */
    if (!done) { ata_dma_why = 2; return -1; }                 /* transfer never finished (timeout) */
    if (ata_wait_busy() < 0) { ata_dma_why = 4; return -1; }
    if (inb(ATA_PRIMARY_STATUS) & (ATA_SR_ERR | ATA_SR_DF)) { ata_dma_why = 5; return -1; }
    if (ata_finish_write(used48) != 0) { ata_dma_why = 6; return -1; }
    if (ata_dma_logged < 6 || ata_dma_waited > 1000000000ULL) {      /* the first few, and anything slower than ~0.4 s */
        if (ata_dma_logged < 60) {
            char b[120]; int k = 0;
            ata_log_str(b, &k, "[ata] dma ok lba="); ata_log_num(b, &k, lba);
            ata_log_str(b, &k, " sectors="); ata_log_num(b, &k, count);
            ata_log_str(b, &k, " waited="); ata_log_num(b, &k, ata_dma_waited / 1000000);
            ata_log_str(b, &k, "Mc polls="); ata_log_num(b, &k, ata_dma_polls);
            b[k++] = '\n'; vga_serial_write(b, (uint64_t)k);
        }
        ata_dma_logged++;
    }
    return 0;
}

/* Write without DMA: WRITE MULTIPLE if it is on, plain WRITE SECTORS otherwise. */
static int ata_write_pio(uint64_t lba, uint8_t count, const void* buf) {
    if (ata_mult > 1 && count > 1) {
        if (ata_write_multi(lba, count, buf) == 0) return 0;
        ata_mult = 0;                    /* see ata_write_sectors(): stop using it, redo the write */
    }
    return ata_write_single(lba, count, buf);
}

static void ata_log_num(char* b, int* k, uint64_t v) {
    char t[24]; int l = 0; if (!v) t[l++] = '0'; while (v) { t[l++] = (char)('0' + v % 10); v /= 10; } while (l) b[(*k)++] = t[--l];
}
static void ata_log_str(char* b, int* k, const char* a) { while (*a) b[(*k)++] = *a++; }
static void ata_log_dma_fallback(uint64_t lba, uint8_t count, const char* verdict) {
    char b[480]; int k = 0;
    const char* a = verdict;
    while (*a) b[k++] = *a++;
    ata_log_num(b, &k, (uint64_t)ata_dma_why);
    a = " (1 issue,2 timeout,3 ctrl-err,4 busy,5 drive-err,6 flush) bm-status="; while (*a) b[k++] = *a++;
    ata_log_num(b, &k, ata_dma_bst);
    a = " lba="; while (*a) b[k++] = *a++;
    ata_log_num(b, &k, lba);
    a = " sectors="; while (*a) b[k++] = *a++;
    ata_log_num(b, &k, count);
    ata_log_str(b, &k, " waited="); ata_log_num(b, &k, ata_dma_waited / 1000000);
    ata_log_str(b, &k, "Mc polls="); ata_log_num(b, &k, ata_dma_polls);
    ata_log_str(b, &k, " drive-status="); ata_log_num(b, &k, ata_dma_dst);
    ata_log_str(b, &k, " drive-error="); ata_log_num(b, &k, ata_dma_err);
    ata_log_str(b, &k, " pci-cmd="); ata_log_num(b, &k, ata_dma_pcicmd);
    ata_log_str(b, &k, " bm-pre="); ata_log_num(b, &k, ata_dma_pre);
    ata_log_str(b, &k, " bm-cmd-before-start="); ata_log_num(b, &k, ata_dma_cmdb);
    ata_log_str(b, &k, " prd-readback="); ata_log_num(b, &k, ata_dma_prdrb);
    ata_log_str(b, &k, " phys-data="); ata_log_num(b, &k, ata_dma_ph0);
    ata_log_str(b, &k, " phys-prd="); ata_log_num(b, &k, ata_dma_php);
    b[k++] = '\n';
    vga_serial_write(b, (uint64_t)k);
}
/* One DMA write. If the controller stalls (the command never starts or never ends), reset the
 * bus master and try again once; the same data written twice is harmless. */
static int ata_write_dma(uint64_t lba, uint8_t count, const void* buf) {
    int r = ata_dma_attempt(lba, count, buf);
    if (r == 0) return 0;
    if (ata_dma_why == 2 && ata_dma_recoveries < 2) {
        ata_dma_recoveries++;
        ata_log_dma_fallback(lba, count, "[ata] DMA command stalled; resetting the bus master and retrying once. reason=");
        outb((uint16_t)(ata_bm + 0), 0x00);
        outb((uint16_t)(ata_bm + 2), (uint8_t)(inb((uint16_t)(ata_bm + 2)) | 0x06));
        ata_wait_busy();
        if (ata_pci_ok) pci_enable_device(&ata_pci);
        r = ata_dma_attempt(lba, count, buf);
        if (r == 0) {
            const char m[] = "[ata] DMA works again after the reset\n";
            vga_serial_write(m, sizeof m - 1);
        }
    }
    return r;
}

static int ata_write_sectors_inner(uint64_t lba, uint8_t count, const void* buf);
static int ata_write_counted(uint64_t lba, uint8_t count, const void* buf) {
    uint64_t t0 = ata_tsc();
    int r = ata_write_sectors_inner(lba, count, buf);
    ata_stats.wr_cmds++; ata_stats.wr_sectors += count; ata_stats.wr_cycles += ata_tsc() - t0;
    return r;
}
/* An ordinary write. A flush left pending by an earlier _nf write happens first, so file data
 * always reaches the disk before any metadata that is written after it. */
int ata_write_sectors(uint64_t lba, uint8_t count, const void* buf) {
    if (ata_flush_pending) ata_flush_now();
    return ata_write_counted(lba, count, buf);
}
/* Write without flushing the drive's cache afterwards. Used for plain file data. */
int ata_write_sectors_nf(uint64_t lba, uint8_t count, const void* buf) {
    ata_defer_flush = 1;
    int r = ata_write_counted(lba, count, buf);
    ata_defer_flush = 0;
    return r;
}

/* Read by bus-master DMA: the controller fills the bounce buffer, which is then copied out.
 * Same addresses, bus-mastering check and wait as the write path; any failure is reported to the
 * caller, which redoes the read by PIO. */
static int ata_read_dma(uint64_t lba, uint8_t count, void* buf) {
    uint32_t bytes = (uint32_t)count * 512;
    if (!count || !ata_phys_buf || !ata_phys_prd || bytes > ATA_DMA_BYTES) { ata_dma_why = 1; return -1; }
    if (ata_pci_ok) {
        ata_dma_pcicmd = pci_config_read32(ata_pci.bus, ata_pci.dev, ata_pci.func, 0x04) & 0xFFFF;
        if (!(ata_dma_pcicmd & 0x04)) { pci_enable_device(&ata_pci); ata_dma_bme_fixed++; }
    }
    uint64_t ph0 = ata_phys_buf, php = ata_phys_prd;
    uint32_t first = bytes > 65536 ? 65536 : bytes;
    ata_dma.prd[0] = (uint32_t)ph0;
    ata_dma.prd[1] = (first == 65536 ? 0u : first) | (bytes <= 65536 ? 0x80000000u : 0u);
    if (bytes > 65536) {
        ata_dma.prd[2] = (uint32_t)(ph0 + 65536);
        ata_dma.prd[3] = (bytes - 65536) | 0x80000000u;
    }
    outl((uint16_t)(ata_bm + 4), (uint32_t)php);
    outb((uint16_t)(ata_bm + 2), (uint8_t)(inb((uint16_t)(ata_bm + 2)) | 0x06));
    outb((uint16_t)(ata_bm + 0), 0x08);                                   /* direction: drive -> memory; stopped */
    int used48;
    ata_irq_arm();
    if (ata_issue(lba, count, 0, 2, &used48) != 0) { ata_irq_disarm(); ata_dma_why = 1; return -1; }
    outb((uint16_t)(ata_bm + 0), 0x09);                                   /* start, drive -> memory */
    int done = 0;
    uint64_t wt0 = ata_tsc(), polls = 0;
    for (;;) {
        uint8_t st = inb((uint16_t)(ata_bm + 2));
        polls++;
        if (st & 0x02) break;
        if (!(st & 0x01)) { done = 1; break; }
        uint64_t now = ata_tsc();
        if (now - wt0 > ata_dma_timeout_cycles) break;
        if (ata_irq_mode) { ata_sleep_for_irq(); continue; }               /* sleep until the drive interrupts */
        uint64_t until = now + 20000;
        while (ata_tsc() < until) __asm__ volatile("pause");
    }
    ata_irq_disarm();
    ata_dma_waited = ata_tsc() - wt0; ata_dma_polls = polls;
    ata_dma_dst = inb(ATA_PRIMARY_STATUS); ata_dma_err = inb(ATA_PRIMARY_ERROR);
    outb((uint16_t)(ata_bm + 0), 0x00);                                   /* stop */
    uint8_t bst = inb((uint16_t)(ata_bm + 2));
    outb((uint16_t)(ata_bm + 2), (uint8_t)(bst | 0x06));
    ata_dma_bst = bst;
    if (bst & 0x02) { ata_dma_why = 3; return -1; }
    if (!done)      { ata_dma_why = 2; return -1; }
    if (ata_wait_busy() < 0) { ata_dma_why = 4; return -1; }
    if (inb(ATA_PRIMARY_STATUS) & (ATA_SR_ERR | ATA_SR_DF)) { ata_dma_why = 5; return -1; }
    uint8_t* dst = (uint8_t*)buf;
    for (uint32_t i = 0; i < bytes; i++) dst[i] = ata_dma.buf[i];
    return 0;
}
static void ata_log_read_dma_failed(uint64_t lba, uint8_t count) {
    ata_log_dma_fallback(lba, count, "[ata] DMA READ FAILED, using PIO from now on. reason=");
}
static int ata_write_sectors_inner(uint64_t lba, uint8_t count, const void* buf) {
    if (ata_dma_ok) {
        if (ata_write_dma(lba, count, buf) == 0) { ata_stats.dma_cmds++; return 0; }
        /* The controller failed or timed out: never use DMA again and redo the write by PIO
         * (writing the same data twice is harmless). */
        ata_dma_ok = 0;
        ata_log_dma_fallback(lba, count, "[ata] DMA write FAILED, using PIO from now on. reason=");
    }
    ata_stats.pio_cmds++;
    return ata_write_pio(lba, count, buf);
}

int ata_dma_active(void) { return ata_dma_ok; }
int ata_irq_active(void) { return ata_irq_mode; }

/* Switch to interrupt-driven waits -- but only if IRQ14 is really delivered: run one small DMA read
 * with the mode on and require that the interrupt was counted. Anything else leaves polling in place. */
int ata_irq_init(void) {
    if (!ata_dma_ok || !ata_dma_read_ok) return 0;          /* the self-test transfer is a DMA read */
    static uint8_t tmp[8 * 512];
    pic_unmask(IRQ_ATA1);
    ata_irq_mode = 1;
    ata_irq_disarm();                                        /* nIEN=1 except while a wait is armed */
    uint32_t c0 = ata_irq_count;
    int r = ata_read_dma(0, 8, tmp);
    if (r != 0 || ata_irq_count == c0) {
        ata_irq_mode = 0;
        outb(ATA_PRIMARY_CTRL, 0x00);
        pic_mask(IRQ_ATA1);
        const char m[] = "[ata] interrupt-driven waits: not used (IRQ14 was not seen; polling stays)\n";
        vga_serial_write(m, sizeof m - 1);
        return 0;
    }
    const char m[] = "[ata] interrupt-driven waits: enabled (IRQ14 self-test passed)\n";
    vga_serial_write(m, sizeof m - 1);
    return 1;
}
int ata_dma_reads_active(void) { return ata_dma_ok && ata_dma_read_ok; }

/* Find the PCI IDE controller, enable bus mastering and PROVE that DMA writes work before
 * anything depends on them. The proof writes patterns into 256 sectors near the end of the
 * disk that currently read as all zeros, reads them back, and checks that the memory around
 * the DMA buffers is untouched; then it zeroes the sectors again. Returns 1 on success. */
int ata_enable_dma(void) {
    static uint8_t exp[255 * 512], back[256 * 512], zeros[255 * 512];
    pci_device_t d;
    int found = 0;
    static const uint8_t prog_ifs[] = { 0x80, 0x8A, 0x8F, 0x85 };         /* bus-master capable IDE variants */
    for (unsigned i = 0; i < sizeof prog_ifs && !found; i++)
        found = pci_find_by_class(0x01, 0x01, prog_ifs[i], &d);
    if (!found) found = pci_find_by_id(0x8086, 0x7010, &d) || pci_find_by_id(0x8086, 0x7111, &d);   /* PIIX3 / PIIX4 */
    if (!found) return -1;
    ata_pci = d; ata_pci_ok = 1;
    uint32_t base = pci_bar_addr(&d, 4);
    if (!base || !(d.bar[4] & 1) || base > 0xFFF0) return -1;             /* BAR4 must be an I/O range */
    pci_enable_device(&d);                                                /* I/O space + bus mastering */

    /* the bounce buffer must be physically contiguous, 64KB aligned and below 4GB */
    uint64_t p0 = vmm_get_phys(&kernel_as, (uint64_t)(uintptr_t)ata_dma.buf);
    uint64_t p1 = vmm_get_phys(&kernel_as, (uint64_t)(uintptr_t)ata_dma.buf + 65536);
    uint64_t pp = vmm_get_phys(&kernel_as, (uint64_t)(uintptr_t)ata_dma.prd);
    if (!p0 || (p0 & 0xFFFF) || p1 != p0 + 65536 || p0 + ATA_DMA_BYTES > 0xFFFFFFFFull || !pp || pp > 0xFFFFFFFFull) return -1;
    if (ata_sectors < 8192) return -1;

    /* scratch: 256 sectors (a multiple of 256, near the end of the disk) that read as all zeros */
    uint64_t lba = 0; int fz = 0;
    for (int t = 0; t < 4 && !fz; t++) {
        uint64_t cand = ((ata_sectors - 512) & ~255ull) - (uint64_t)t * 1024;
        if (ata_read_sectors(cand, 255, back) != 0) return -1;
        if (ata_read_sectors(cand + 255, 1, back + 255 * 512) != 0) return -1;
        int allzero = 1;
        for (int i = 0; i < 256 * 512; i++) if (back[i]) { allzero = 0; break; }
        if (allzero) { lba = cand; fz = 1; }
    }
    if (!fz) return -1;

    for (unsigned i = 0; i < sizeof ata_dma.pad_lo; i++)  ata_dma.pad_lo[i]  = 0xA5;
    for (unsigned i = 0; i < sizeof ata_dma.pad_hi; i++)  ata_dma.pad_hi[i]  = 0xA5;
    for (unsigned i = 0; i < sizeof ata_dma.pad_prd; i++) ata_dma.pad_prd[i] = 0xA5;

    ata_bm = (uint16_t)base;
    ata_phys_buf = p0; ata_phys_prd = pp; ata_phys_warned = 0;                       /* used by every DMA command from now on */
    for (int i = 0; i < 255 * 512; i++) exp[i] = (uint8_t)((i * 7 + 13) | 1);        /* never zero */
    int ok = (ata_write_dma(lba, 255, exp) == 0);                                    /* a full-size transfer: two PRD entries */
    if (ok) {                                                                        /* overwrite 8 sectors (one filesystem block) and 1 sector */
        static uint8_t p8[8 * 512], p1b[512];
        for (int i = 0; i < 8 * 512; i++) { p8[i] = (uint8_t)((i * 11 + 5) | 1); exp[10 * 512 + i] = p8[i]; }
        for (int i = 0; i < 512; i++)     { p1b[i] = (uint8_t)((i * 3 + 9) | 1);  exp[200 * 512 + i] = p1b[i]; }
        ok = (ata_write_dma(lba + 10, 8, p8) == 0) && (ata_write_dma(lba + 200, 1, p1b) == 0);
    }
    if (ok) ok = (ata_read_sectors(lba, 255, back) == 0);
    if (ok) for (int i = 0; i < 255 * 512; i++) if (back[i] != exp[i]) { ok = 0; break; }
    int read_ok = 0;
    if (ok) {                                                                        /* DMA READ: must deliver exactly what the disk holds */
        static uint8_t r8[8 * 512];
        for (int i = 0; i < 255 * 512; i++) back[i] = 0xEE;                          /* canary: "no data arrived" cannot pass */
        for (int i = 0; i < 8 * 512; i++)   r8[i]   = 0xEE;
        read_ok = (ata_read_dma(lba, 255, back) == 0) && (ata_read_dma(lba + 10, 8, r8) == 0);
        if (read_ok) for (int i = 0; i < 255 * 512; i++) if (back[i] != exp[i]) { read_ok = 0; break; }
        if (read_ok) for (int i = 0; i < 8 * 512; i++)   if (r8[i] != exp[10 * 512 + i]) { read_ok = 0; break; }
    }
    if (ok) {                                                                        /* nothing may have been written outside the buffer */
        for (unsigned i = 0; i < sizeof ata_dma.pad_lo; i++)  if (ata_dma.pad_lo[i]  != 0xA5) { ok = 0; break; }
        for (unsigned i = 0; ok && i < sizeof ata_dma.pad_hi; i++)  if (ata_dma.pad_hi[i]  != 0xA5) ok = 0;
        for (unsigned i = 0; ok && i < sizeof ata_dma.pad_prd; i++) if (ata_dma.pad_prd[i] != 0xA5) ok = 0;
    }
    /* put the zeros back using the proven non-DMA path, whatever happened */
    ata_dma_ok = 0;
    int restored = (ata_write_pio(lba, 255, zeros) == 0) && (ata_write_pio(lba + 255, 1, zeros) == 0);
    if (!ok || !restored) { ata_bm = 0; ata_phys_buf = ata_phys_prd = 0; ata_dma_read_ok = 0; return -1; }
    ata_dma_ok = 1;
    ata_dma_read_ok = read_ok;
    { const char* m = read_ok ? "[ata] DMA reads: enabled (self-test passed)\n" : "[ata] DMA reads: not used (self-test failed; reads stay PIO)\n";
      int n = 0; while (m[n]) n++; vga_serial_write(m, (uint64_t)n); }
    return 1;
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
