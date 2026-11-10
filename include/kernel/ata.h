#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H

#include <stdint.h>

/* ATA I/O ports (primary bus) */
#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_SECCOUNT    0x1F2
#define ATA_PRIMARY_LBA_LO      0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HI      0x1F5
#define ATA_PRIMARY_DRIVE       0x1F6
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7
#define ATA_PRIMARY_CTRL        0x3F6

/* ATA status bits */
#define ATA_SR_BSY              0x80
#define ATA_SR_DRDY             0x40
#define ATA_SR_DRQ              0x08
#define ATA_SR_ERR              0x01
#define ATA_SR_DF               0x20   /* device fault */

/* ATA commands */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_READ_PIO_EXT    0x24   /* LBA48 */
#define ATA_CMD_READ_DMA        0xC8
#define ATA_CMD_READ_DMA_EXT    0x25   /* LBA48 */
#define ATA_CMD_WRITE_PIO_EXT   0x34   /* LBA48 */
#define ATA_CMD_SET_MULTIPLE    0xC6   /* SET MULTIPLE MODE: sectors per DRQ block */
#define ATA_CMD_WRITE_MULTIPLE  0xC5
#define ATA_CMD_WRITE_MULTIPLE_EXT 0x39  /* LBA48 */
#define ATA_CMD_WRITE_DMA       0xCA
#define ATA_CMD_WRITE_DMA_EXT   0x35   /* LBA48 */
#define ATA_CMD_FLUSH           0xE7
#define ATA_CMD_FLUSH_EXT       0xEA

/* Sector size */
#define ATA_SECTOR_SIZE         512

/* Initialize ATA driver — returns 1 if disk found, 0 if not */
int  ata_init(void);

/* Read count sectors (1..255) starting at lba into buf.
 * LBAs below 2^28 (first 128 GB) use the classic 28-bit commands exactly
 * as before; higher LBAs use 48-bit commands (needs a drive that reports
 * LBA48 support, otherwise returns -1). */
int  ata_read_sectors(uint64_t lba, uint8_t count, void* buf);

/* Write count sectors (1..255) starting at lba from buf */
int  ata_write_sectors(uint64_t lba, uint8_t count, const void* buf);

/* Total addressable sectors reported by IDENTIFY (0 if no drive). */
uint64_t ata_capacity_sectors(void);

/* Multiple-sector writes (ATA WRITE MULTIPLE): enabled and verified at boot by
 * ata_init(). ata_enable_multiple() returns the block size in sectors, or -1
 * if the drive lacks it or the self-test failed (plain writes are then used). */
int     ata_enable_multiple(uint8_t max_sectors_per_block);

/* Bus-master DMA writes (PCI IDE controller): one host write per command instead of
 * one per 8KB. ata_enable_dma() finds the controller, runs a self-test (data readback
 * and memory canaries around the DMA buffers) and returns 1 on success, -1 if DMA
 * is unavailable or failed the test (PIO writes keep working). */
int     ata_enable_dma(void);
int     ata_dma_active(void);
int     ata_dma_reads_active(void);
/* Interrupt-driven waits. ata_irq_init() must run after multitasking and the PIC are up; it turns the
 * mode on only if a test transfer really raised IRQ14, and returns 1 if it is on. ata_irq_handler()
 * is called from the IRQ14 stub. */
int     ata_irq_init(void);
void    ata_irq_handler(void);
int     ata_irq_active(void);   /* DMA reads passed their own boot self-test and are in use */
uint8_t ata_multiple_sectors(void);   /* 0 = not in use */


/* Diagnostics: counts and TSC cycles spent in disk commands since the last take.
 * wr_* includes the cache flush (fl_* is that flush alone). */
typedef struct {
    uint64_t rd_cmds, rd_sectors, rd_cycles;
    uint64_t wr_cmds, wr_sectors, wr_cycles;
    uint64_t fl_cmds, fl_cycles;
    uint64_t dma_cmds, pio_cmds;      /* write commands that went by DMA / by PIO */
    uint64_t rd_dma_cmds;             /* read commands that went by DMA */
} ata_stats_t;
extern ata_stats_t ata_stats;
void ata_stats_take(ata_stats_t* out);

/* Write without the cache flush; the flush is done later, by ata_flush_now() or automatically
 * before the next ordinary write (so data reaches the disk before any metadata written after it). */
int  ata_write_sectors_nf(uint64_t lba, uint8_t count, const void* buf);
int  ata_flush_now(void);          /* flush if an earlier _nf write left one pending */

#endif
