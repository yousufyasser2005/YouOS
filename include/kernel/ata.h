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
#define ATA_CMD_WRITE_PIO_EXT   0x34   /* LBA48 */
#define ATA_CMD_SET_MULTIPLE    0xC6   /* SET MULTIPLE MODE: sectors per DRQ block */
#define ATA_CMD_WRITE_MULTIPLE  0xC5
#define ATA_CMD_WRITE_MULTIPLE_EXT 0x39  /* LBA48 */
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
uint8_t ata_multiple_sectors(void);   /* 0 = not in use */

#endif
