#ifndef KERNEL_FAT16_H
#define KERNEL_FAT16_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} fat16_bpb_t;

typedef struct __attribute__((packed)) {
    char     name[8];
    char     ext[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster;
    uint32_t file_size;
} fat16_dirent_t;

#define FAT16_ATTR_READONLY  0x01
#define FAT16_ATTR_HIDDEN    0x02
#define FAT16_ATTR_SYSTEM    0x04
#define FAT16_ATTR_VOLUME_ID 0x08
#define FAT16_ATTR_DIRECTORY 0x10
#define FAT16_ATTR_ARCHIVE   0x20
#define FAT16_ATTR_LFN       0x0F

#define FAT16_EOC 0xFFF8

int     fat16_init(void);
int     fat16_open(const char* path);
int     fat16_read(int fd, void* buf, uint32_t size);
int     fat16_close(int fd);
int     fat16_write(int fd, const void* buf, uint32_t size);
int     fat16_create(const char* path);

#endif
