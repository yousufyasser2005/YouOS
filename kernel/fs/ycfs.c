/*
 * ycfs.c — YCFS core driver (Phase 1: read-only)
 *
 * Directories are just files whose data is a stream of 64-byte
 * ycfs_dirent_t records — this is what gives real nested directory
 * trees, unlike FAT16's flat root. Free-space bitmaps and a self-
 * describing superblock (all layout offsets stored on-disk) mean the
 * driver never hardcodes geometry beyond what mkycfs.py wrote there.
 */

#include <kernel/ycfs.h>
#include <kernel/ata.h>
#include <kernel/vga.h>

static ycfs_superblock_t sb;
static int initialized = 0;

#define SECTORS_PER_BLOCK (YCFS_BLOCK_SIZE / 512)

static int read_block(uint32_t block_num, void* buf) {
    uint32_t lba = YCFS_START_LBA + block_num * SECTORS_PER_BLOCK;
    return ata_read_sectors(lba, (uint8_t)SECTORS_PER_BLOCK, buf);
}

static int read_inode(uint32_t inode_num, ycfs_inode_t* out) {
    if (inode_num == 0 || inode_num >= sb.total_inodes) return -1;
    uint32_t inodes_per_block = YCFS_BLOCK_SIZE / sizeof(ycfs_inode_t);
    uint32_t block  = sb.inode_table_block + inode_num / inodes_per_block;
    uint32_t idx_in = inode_num % inodes_per_block;

    static uint8_t blockbuf[YCFS_BLOCK_SIZE];
    if (read_block(block, blockbuf) != 0) return -1;
    ycfs_inode_t* table = (ycfs_inode_t*)blockbuf;
    *out = table[idx_in];
    return 0;
}

int ycfs_init(void) {
    static uint8_t blockbuf[YCFS_BLOCK_SIZE];
    uint32_t lba = YCFS_START_LBA;
    if (ata_read_sectors(lba, (uint8_t)SECTORS_PER_BLOCK, blockbuf) != 0) {
        vga_puts_color("  [!!] YCFS: superblock read failed\n", VGA_LIGHT_RED, VGA_BLACK);
        return -1;
    }
    ycfs_superblock_t* s = (ycfs_superblock_t*)blockbuf;
    if (s->magic != YCFS_MAGIC) {
        vga_puts_color("  [!!] YCFS: bad magic (not formatted?)\n", VGA_LIGHT_RED, VGA_BLACK);
        return -1;
    }
    sb = *s;
    initialized = 1;
    vga_puts_color("  [OK] YCFS filesystem mounted\n", VGA_LIGHT_GREEN, VGA_BLACK);
    return 0;
}

uint32_t ycfs_root_inode(void) {
    return sb.root_inode;
}

/* Read `size` bytes starting at `offset` from the given inode's data,
 * walking direct pointers and (if needed) the single indirect block. */
int64_t ycfs_read(uint32_t inode_num, uint64_t offset, uint32_t size, void* buf) {
    if (!initialized) return -1;
    ycfs_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) return -1;

    uint8_t* dst = (uint8_t*)buf;
    uint32_t bytes_read = 0;
    static uint8_t blockbuf[YCFS_BLOCK_SIZE];
    static uint32_t indirect_ptrs[YCFS_BLOCK_SIZE / 4];
    uint32_t cached_indirect_block = 0xFFFFFFFFu;

    while (bytes_read < size && offset < inode.size) {
        uint32_t block_idx = (uint32_t)(offset / YCFS_BLOCK_SIZE);
        uint32_t block_off = (uint32_t)(offset % YCFS_BLOCK_SIZE);
        uint32_t phys_block;

        if (block_idx < YCFS_DIRECT_BLOCKS) {
            phys_block = inode.direct[block_idx];
        } else {
            uint32_t indirect_idx = block_idx - YCFS_DIRECT_BLOCKS;
            uint32_t ptrs_per_block = YCFS_BLOCK_SIZE / 4;
            if (indirect_idx >= ptrs_per_block || inode.indirect == 0) break;
            if (cached_indirect_block != inode.indirect) {
                if (read_block(inode.indirect, indirect_ptrs) != 0) break;
                cached_indirect_block = inode.indirect;
            }
            phys_block = indirect_ptrs[indirect_idx];
        }
        if (!phys_block) break;
        if (read_block(phys_block, blockbuf) != 0) break;

        uint32_t chunk = YCFS_BLOCK_SIZE - block_off;
        uint32_t remaining_want = size - bytes_read;
        uint64_t remaining_file = inode.size - offset;
        if (chunk > remaining_want) chunk = remaining_want;
        if ((uint64_t)chunk > remaining_file) chunk = (uint32_t)remaining_file;

        for (uint32_t k = 0; k < chunk; k++) dst[bytes_read + k] = blockbuf[block_off + k];
        bytes_read += chunk;
        offset     += chunk;
    }
    return (int64_t)bytes_read;
}

/* Scan a directory inode's dirent stream for `name`. */
int ycfs_lookup(uint32_t dir_inode, const char* name,
                uint32_t* out_inode, uint32_t* out_type, uint64_t* out_size) {
    if (!initialized) return -1;
    ycfs_inode_t dir;
    if (read_inode(dir_inode, &dir) != 0) return -1;
    if (dir.mode != YCFS_TYPE_DIR) return -1;

    static uint8_t dirbuf[YCFS_BLOCK_SIZE];
    uint64_t remaining = dir.size;
    uint64_t offset = 0;

    while (remaining > 0) {
        uint32_t chunk = remaining > YCFS_BLOCK_SIZE ? YCFS_BLOCK_SIZE : (uint32_t)remaining;
        int64_t n = ycfs_read(dir_inode, offset, chunk, dirbuf);
        if (n <= 0) break;

        uint32_t entries = (uint32_t)n / sizeof(ycfs_dirent_t);
        ycfs_dirent_t* ents = (ycfs_dirent_t*)dirbuf;
        for (uint32_t i = 0; i < entries; i++) {
            if (ents[i].inode == 0) continue;
            int match = 1;
            for (int k = 0; k < 56; k++) {
                if (ents[i].name[k] != name[k]) { match = 0; break; }
                if (ents[i].name[k] == 0) break;
            }
            if (match) {
                ycfs_inode_t child;
                if (read_inode(ents[i].inode, &child) != 0) return -1;
                *out_inode = ents[i].inode;
                *out_type  = child.mode;
                *out_size  = child.size;
                return 0;
            }
        }
        offset    += n;
        remaining -= (uint64_t)n;
    }
    return -1;
}
