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

/* ATA commands */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_IDENTIFY        0xEC

/* Sector size */
#define ATA_SECTOR_SIZE         512

/* Initialize ATA driver — returns 1 if disk found, 0 if not */
int  ata_init(void);

/* Read count sectors starting at lba into buf */
int  ata_read_sectors(uint32_t lba, uint8_t count, void* buf);

/* Write count sectors starting at lba from buf */
int  ata_write_sectors(uint32_t lba, uint8_t count, const void* buf);

#endif
