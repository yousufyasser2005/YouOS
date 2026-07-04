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
static void ycfs_journal_replay(void);

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
    ycfs_journal_replay();
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

/* ==================== Phase 2: writes ==================== */

static int raw_write_block(uint32_t block_num, const void* buf) {
    uint32_t lba = YCFS_START_LBA + block_num * SECTORS_PER_BLOCK;
    return ata_write_sectors(lba, (uint8_t)SECTORS_PER_BLOCK, buf);
}

/* ---- journaling machinery ----
 * current_txn_id != 0 means every write_block() call between txn_begin()
 * and txn_commit() gets logged to the journal (write-ahead) before the
 * real write happens. journal_pos is in-memory only and resets to 0 each
 * boot, since ycfs_journal_replay() always wipes the journal region
 * after replaying it. */
static uint32_t current_txn_id = 0;
static uint32_t journal_pos    = 0;
static uint32_t next_txn_id    = 1;

static int journal_log(uint32_t txn_id, uint32_t target_block, const void* data) {
    if (sb.journal_num_blocks == 0) return -1; /* unformatted / no journal */
    if (journal_pos + 2 > sb.journal_num_blocks) journal_pos = 0;
    ycfs_journal_hdr_t hdr;
    uint8_t* hp = (uint8_t*)&hdr;
    for (uint32_t i = 0; i < sizeof(hdr); i++) hp[i] = 0;
    hdr.magic        = YCFS_JOURNAL_MAGIC;
    hdr.txn_id       = txn_id;
    hdr.target_block = target_block;
    hdr.type         = YCFS_JTYPE_DESC;
    raw_write_block(sb.journal_start_block + journal_pos, &hdr);
    raw_write_block(sb.journal_start_block + journal_pos + 1, data);
    journal_pos += 2;
    return 0;
}

static uint32_t txn_begin(void) {
    current_txn_id = next_txn_id++;
    return current_txn_id;
}

static void txn_commit(void) {
    if (current_txn_id == 0) return;
    if (sb.journal_num_blocks != 0) {
        if (journal_pos + 1 > sb.journal_num_blocks) journal_pos = 0;
        ycfs_journal_hdr_t hdr;
        uint8_t* hp = (uint8_t*)&hdr;
        for (uint32_t i = 0; i < sizeof(hdr); i++) hp[i] = 0;
        hdr.magic  = YCFS_JOURNAL_MAGIC;
        hdr.txn_id = current_txn_id;
        hdr.type   = YCFS_JTYPE_COMMIT;
        raw_write_block(sb.journal_start_block + journal_pos, &hdr);
        journal_pos += 1;
    }
    current_txn_id = 0;
}

static int write_block(uint32_t block_num, const void* buf) {
    if (current_txn_id != 0) journal_log(current_txn_id, block_num, buf);
    return raw_write_block(block_num, buf);
}

/* Replay any committed-but-possibly-unapplied transactions, discard any
 * incomplete (no matching commit) ones, then wipe the journal so the
 * next boot starts clean. Safe to call even if journal_num_blocks == 0
 * (old/unformatted superblock) — becomes a no-op. */
static void ycfs_journal_replay(void) {
    if (sb.journal_num_blocks == 0) return;

    #define YCFS_MAX_STAGE 32
    static uint8_t  hdrbuf[YCFS_BLOCK_SIZE];
    static uint8_t  databuf[YCFS_BLOCK_SIZE];
    static uint32_t stage_targets[YCFS_MAX_STAGE];
    static uint8_t  stage_data[YCFS_MAX_STAGE][YCFS_BLOCK_SIZE];
    uint32_t stage_count = 0;
    uint32_t stage_txn   = 0;
    int replayed_any = 0;

    uint32_t pos = 0;
    while (pos < sb.journal_num_blocks) {
        if (read_block(sb.journal_start_block + pos, hdrbuf) != 0) break;
        ycfs_journal_hdr_t* hdr = (ycfs_journal_hdr_t*)hdrbuf;
        if (hdr->magic != YCFS_JOURNAL_MAGIC) break;

        if (hdr->type == YCFS_JTYPE_DESC) {
            if (stage_count == 0) stage_txn = hdr->txn_id;
            if (hdr->txn_id != stage_txn) {
                /* a different transaction started without the previous
                 * one committing — the previous group is incomplete */
                stage_count = 0;
                stage_txn   = hdr->txn_id;
            }
            if (pos + 1 >= sb.journal_num_blocks) break;
            if (read_block(sb.journal_start_block + pos + 1, databuf) != 0) break;
            if (stage_count < YCFS_MAX_STAGE) {
                stage_targets[stage_count] = hdr->target_block;
                for (int i = 0; i < YCFS_BLOCK_SIZE; i++) stage_data[stage_count][i] = databuf[i];
                stage_count++;
            }
            pos += 2;
        } else if (hdr->type == YCFS_JTYPE_COMMIT) {
            if (hdr->txn_id == stage_txn && stage_count > 0) {
                for (uint32_t i = 0; i < stage_count; i++)
                    raw_write_block(stage_targets[i], stage_data[i]);
                replayed_any = 1;
            }
            stage_count = 0;
            stage_txn   = 0;
            pos += 1;
        } else {
            break;
        }
    }

    static uint8_t zero[YCFS_BLOCK_SIZE];
    for (int i = 0; i < YCFS_BLOCK_SIZE; i++) zero[i] = 0;
    for (uint32_t i = 0; i < sb.journal_num_blocks; i++)
        raw_write_block(sb.journal_start_block + i, zero);
    journal_pos = 0;

    if (replayed_any)
        vga_puts_color("  [OK] YCFS journal: replayed pending transaction(s)\n", VGA_LIGHT_GREEN, VGA_BLACK);
}

/* Deliberately simulates both crash scenarios to prove replay makes the
 * right call in each, rather than just hoping the logic is correct:
 *   1. An incomplete transaction (logged, no commit) must be discarded.
 *   2. A complete transaction (logged + committed) whose real write never
 *      landed before the "crash" must still get applied by replay.
 * Uses the volume's last block as scratch space (well past any real
 * data at this filesystem's size). Non-destructive to real content. */
int ycfs_journal_self_test(void) {
    if (sb.journal_num_blocks < 6) return -1;
    uint32_t scratch = sb.total_blocks - 1;
    static uint8_t testblock[YCFS_BLOCK_SIZE];
    static uint8_t check[YCFS_BLOCK_SIZE];

    for (int i = 0; i < YCFS_BLOCK_SIZE; i++) testblock[i] = 0;
    raw_write_block(scratch, testblock);

    /* Test 1: log a write but never commit it, and never perform the
     * real write either (simulating a crash right after logging). */
    uint32_t txn1 = 999000;
    for (int i = 0; i < YCFS_BLOCK_SIZE; i++) testblock[i] = 0xAA;
    journal_log(txn1, scratch, testblock);
    ycfs_journal_replay();
    read_block(scratch, check);
    int test1_ok = 1;
    for (int i = 0; i < YCFS_BLOCK_SIZE; i++) if (check[i] != 0) { test1_ok = 0; break; }

    /* Test 2: log a write AND its commit, but skip the real write
     * (simulating a crash after the journal was durable but before the
     * real target update landed) — replay must still apply it. */
    uint32_t txn2 = 999001;
    for (int i = 0; i < YCFS_BLOCK_SIZE; i++) testblock[i] = 0xBB;
    journal_log(txn2, scratch, testblock);
    ycfs_journal_hdr_t hdr;
    uint8_t* hp = (uint8_t*)&hdr;
    for (uint32_t i = 0; i < sizeof(hdr); i++) hp[i] = 0;
    hdr.magic = YCFS_JOURNAL_MAGIC; hdr.txn_id = txn2; hdr.type = YCFS_JTYPE_COMMIT;
    raw_write_block(sb.journal_start_block + journal_pos, &hdr);
    journal_pos += 1;
    ycfs_journal_replay();
    read_block(scratch, check);
    int test2_ok = 1;
    for (int i = 0; i < YCFS_BLOCK_SIZE; i++) if (check[i] != 0xBB) { test2_ok = 0; break; }

    return (test1_ok && test2_ok) ? 0 : -1;
}

static int write_superblock(void) {
    static uint8_t blockbuf[YCFS_BLOCK_SIZE];
    for (int i = 0; i < YCFS_BLOCK_SIZE; i++) blockbuf[i] = 0;
    ycfs_superblock_t* s = (ycfs_superblock_t*)blockbuf;
    *s = sb;
    return write_block(0, blockbuf);
}

static int write_inode(uint32_t inode_num, const ycfs_inode_t* in) {
    uint32_t inodes_per_block = YCFS_BLOCK_SIZE / sizeof(ycfs_inode_t);
    uint32_t block  = sb.inode_table_block + inode_num / inodes_per_block;
    uint32_t idx_in = inode_num % inodes_per_block;
    static uint8_t blockbuf[YCFS_BLOCK_SIZE];
    if (read_block(block, blockbuf) != 0) return -1;
    ycfs_inode_t* table = (ycfs_inode_t*)blockbuf;
    table[idx_in] = *in;
    return write_block(block, blockbuf);
}

/* Find and claim the first free bit in a bitmap block. Caller updates
 * the relevant sb.free_* counter and persists the superblock. */
static int alloc_bit(uint32_t bitmap_block, uint32_t total_bits, uint32_t* out_idx) {
    static uint8_t bm[YCFS_BLOCK_SIZE];
    if (read_block(bitmap_block, bm) != 0) return -1;
    for (uint32_t i = 0; i < total_bits; i++) {
        if (!(bm[i / 8] & (1 << (i % 8)))) {
            bm[i / 8] |= (1 << (i % 8));
            if (write_block(bitmap_block, bm) != 0) return -1;
            *out_idx = i;
            return 0;
        }
    }
    return -1; /* full */
}

static int alloc_block_num(uint32_t* out_block) {
    if (sb.free_blocks == 0) return -1;
    if (alloc_bit(sb.block_bitmap_block, sb.total_blocks, out_block) != 0) return -1;
    sb.free_blocks--;
    write_superblock();
    return 0;
}

static int alloc_inode_num(uint32_t* out_inode) {
    if (sb.free_inodes == 0) return -1;
    if (alloc_bit(sb.inode_bitmap_block, sb.total_inodes, out_inode) != 0) return -1;
    sb.free_inodes--;
    write_superblock();
    return 0;
}

/* Return the physical block backing logical block `block_idx` of
 * `inode`, allocating it (and updating inode->direct[]/indirect) if it
 * doesn't exist yet. Only one indirect level — matches ycfs_read's cap. */
static int get_block_for_write(ycfs_inode_t* inode, uint32_t block_idx, uint32_t* out_phys) {
    if (block_idx < YCFS_DIRECT_BLOCKS) {
        if (inode->direct[block_idx] == 0) {
            uint32_t nb;
            if (alloc_block_num(&nb) != 0) return -1;
            inode->direct[block_idx] = nb;
            inode->blocks_used++;
        }
        *out_phys = inode->direct[block_idx];
        return 0;
    }
    uint32_t indirect_idx  = block_idx - YCFS_DIRECT_BLOCKS;
    uint32_t ptrs_per_block = YCFS_BLOCK_SIZE / 4;
    if (indirect_idx >= ptrs_per_block) return -1; /* phase 2 cap, no double-indirect */

    static uint32_t ptrs[YCFS_BLOCK_SIZE / 4];
    if (inode->indirect == 0) {
        uint32_t nb;
        if (alloc_block_num(&nb) != 0) return -1;
        inode->indirect = nb;
        inode->blocks_used++;
        for (uint32_t i = 0; i < YCFS_BLOCK_SIZE / 4; i++) ptrs[i] = 0;
        if (write_block(nb, ptrs) != 0) return -1;
    }
    if (read_block(inode->indirect, ptrs) != 0) return -1;
    if (ptrs[indirect_idx] == 0) {
        uint32_t nb;
        if (alloc_block_num(&nb) != 0) return -1;
        ptrs[indirect_idx] = nb;
        inode->blocks_used++;
        if (write_block(inode->indirect, ptrs) != 0) return -1;
    }
    *out_phys = ptrs[indirect_idx];
    return 0;
}

/* Write `size` bytes at `offset` into inode_num's data, allocating new
 * blocks as needed and growing inode.size if the write extends past the
 * current end of file. Used for regular file writes AND for appending
 * dirents to a directory's own byte stream (a directory is just a file). */
int64_t ycfs_write(uint32_t inode_num, uint64_t offset, uint32_t size, const void* buf) {
    if (!initialized) return -1;
    ycfs_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) return -1;

    const uint8_t* src = (const uint8_t*)buf;
    uint32_t bytes_written = 0;
    static uint8_t blockbuf[YCFS_BLOCK_SIZE];

    while (bytes_written < size) {
        uint32_t block_idx = (uint32_t)((offset + bytes_written) / YCFS_BLOCK_SIZE);
        uint32_t block_off = (uint32_t)((offset + bytes_written) % YCFS_BLOCK_SIZE);
        uint32_t phys;
        if (get_block_for_write(&inode, block_idx, &phys) != 0) break;

        uint32_t chunk = YCFS_BLOCK_SIZE - block_off;
        uint32_t remaining = size - bytes_written;
        if (chunk > remaining) chunk = remaining;

        if (block_off != 0 || chunk != YCFS_BLOCK_SIZE) {
            if (read_block(phys, blockbuf) != 0) break;
        }
        for (uint32_t k = 0; k < chunk; k++) blockbuf[block_off + k] = src[bytes_written + k];
        if (write_block(phys, blockbuf) != 0) break;

        bytes_written += chunk;
    }

    uint64_t new_end = offset + bytes_written;
    if (new_end > inode.size) inode.size = new_end;
    write_inode(inode_num, &inode);
    return (int64_t)bytes_written;
}

static int append_dirent(uint32_t dir_inode, uint32_t child_inode, const char* name, uint32_t type) {
    ycfs_inode_t dir;
    if (read_inode(dir_inode, &dir) != 0) return -1;

    ycfs_dirent_t d;
    d.inode = child_inode;
    d.rec_len = sizeof(ycfs_dirent_t);
    int nlen = 0; while (name[nlen] && nlen < 55) nlen++;
    d.name_len = (uint8_t)nlen;
    d.file_type = (uint8_t)type;
    for (int i = 0; i < 56; i++) d.name[i] = (i < nlen) ? name[i] : 0;

    int64_t n = ycfs_write(dir_inode, dir.size, sizeof(d), &d);
    return (n == (int64_t)sizeof(d)) ? 0 : -1;
}

/* Create a new file or directory named `name` inside dir_inode.
 * Fails if the name already exists. */
int ycfs_create(uint32_t dir_inode, const char* name, uint32_t type, uint32_t* out_inode) {
    if (!initialized) return -1;

    uint32_t existing_inode, existing_type; uint64_t existing_size;
    if (ycfs_lookup(dir_inode, name, &existing_inode, &existing_type, &existing_size) == 0)
        return -1; /* already exists */

    txn_begin();

    uint32_t new_inode_num;
    if (alloc_inode_num(&new_inode_num) != 0) { txn_commit(); return -1; }

    ycfs_inode_t ni;
    uint8_t* p = (uint8_t*)&ni;
    for (uint32_t i = 0; i < sizeof(ni); i++) p[i] = 0;
    ni.mode        = type;
    ni.size        = 0;
    ni.links_count = 1;
    ni.blocks_used = 0;
    if (write_inode(new_inode_num, &ni) != 0) { txn_commit(); return -1; }

    if (append_dirent(dir_inode, new_inode_num, name, type) != 0) { txn_commit(); return -1; }

    txn_commit();
    if (out_inode) *out_inode = new_inode_num;
    return 0;
}

/* ==================== Phase 2: unlink / mkdir / rename / list ==================== */

static int free_bit(uint32_t bitmap_block, uint32_t idx) {
    static uint8_t bm[YCFS_BLOCK_SIZE];
    if (read_block(bitmap_block, bm) != 0) return -1;
    bm[idx / 8] &= ~(uint8_t)(1 << (idx % 8));
    return write_block(bitmap_block, bm);
}

static void free_block_num(uint32_t block_num) {
    if (free_bit(sb.block_bitmap_block, block_num) == 0) { sb.free_blocks++; write_superblock(); }
}

static void free_inode_num(uint32_t inode_num) {
    if (free_bit(sb.inode_bitmap_block, inode_num) == 0) { sb.free_inodes++; write_superblock(); }
}

/* Strip an optional leading "/ycfs/" or bare "ycfs" prefix, matching
 * fat16's own inline convention for its "/disk/" prefix. */
static const char* strip_ycfs_prefix(const char* path) {
    if (path[0] == '/') path++;
    if (path[0]=='y'&&path[1]=='c'&&path[2]=='f'&&path[3]=='s'&&path[4]=='/') path += 5;
    else if (path[0]=='y'&&path[1]=='c'&&path[2]=='f'&&path[3]=='s'&&path[4]==0) path += 4;
    return path;
}

/* Walk all but the last "/"-separated component of `path`, returning the
 * inode of the final component's parent directory and the component's
 * own name. This is what makes real nested-directory mkdir/unlink/rename
 * possible, unlike FAT16's single flat root_find(). */
static int ycfs_resolve_parent(const char* raw_path, uint32_t* out_parent_inode, char* out_name) {
    const char* path = strip_ycfs_prefix(raw_path);
    uint32_t cur = sb.root_inode;
    char component[56];
    while (1) {
        int i = 0;
        while (path[i] && path[i] != '/' && i < 55) { component[i] = path[i]; i++; }
        component[i] = 0;
        const char* next = path + i;
        if (*next == '/') next++;
        if (*next == 0) {
            *out_parent_inode = cur;
            int k = 0; while (component[k]) { out_name[k] = component[k]; k++; } out_name[k] = 0;
            return (component[0] != 0) ? 0 : -1;
        }
        uint32_t child_inode, child_type; uint64_t child_size;
        if (ycfs_lookup(cur, component, &child_inode, &child_type, &child_size) != 0) return -1;
        if (child_type != YCFS_TYPE_DIR) return -1;
        cur = child_inode;
        path = next;
    }
}

/* Full path resolve (used by list_dir/stat on the target itself, not
 * just its parent). Empty path or "/" means the ycfs root. */
static int ycfs_resolve(const char* raw_path, uint32_t* out_inode, uint32_t* out_type, uint64_t* out_size) {
    const char* path = strip_ycfs_prefix(raw_path);
    if (path[0] == 0) {
        *out_inode = sb.root_inode;
        *out_type  = YCFS_TYPE_DIR;
        *out_size  = 0;
        return 0;
    }
    uint32_t parent; char name[56];
    if (ycfs_resolve_parent(raw_path, &parent, name) != 0) return -1;
    return ycfs_lookup(parent, name, out_inode, out_type, out_size);
}

/* Scan dir_inode's dirent stream for `name` and mark that slot unused
 * (inode = 0). Does not free the target's own inode/blocks — callers
 * that want that (ycfs_unlink) do it separately. */
static int clear_dirent(uint32_t dir_inode, const char* name) {
    ycfs_inode_t dir;
    if (read_inode(dir_inode, &dir) != 0) return -1;

    static uint8_t dirbuf[YCFS_BLOCK_SIZE];
    uint64_t remaining = dir.size, offset = 0;
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
                ents[i].inode = 0;
                ycfs_write(dir_inode, offset + (uint64_t)i * sizeof(ycfs_dirent_t),
                           sizeof(ycfs_dirent_t), &ents[i]);
                return 0;
            }
        }
        offset    += n;
        remaining -= (uint64_t)n;
    }
    return -1;
}

int ycfs_mkdir(const char* path) {
    if (!initialized) return -1;
    uint32_t parent; char name[56];
    if (ycfs_resolve_parent(path, &parent, name) != 0) return -1;
    uint32_t new_inode;
    return ycfs_create(parent, name, YCFS_TYPE_DIR, &new_inode);
}

int ycfs_unlink(const char* path) {
    if (!initialized) return -1;
    uint32_t parent; char name[56];
    if (ycfs_resolve_parent(path, &parent, name) != 0) return -1;

    uint32_t target_inode, target_type; uint64_t target_size;
    if (ycfs_lookup(parent, name, &target_inode, &target_type, &target_size) != 0) return -1;

    txn_begin();

    if (clear_dirent(parent, name) != 0) { txn_commit(); return -1; }

    ycfs_inode_t ti;
    if (read_inode(target_inode, &ti) == 0) {
        for (int i = 0; i < YCFS_DIRECT_BLOCKS; i++)
            if (ti.direct[i]) free_block_num(ti.direct[i]);
        if (ti.indirect) {
            static uint32_t ptrs[YCFS_BLOCK_SIZE / 4];
            if (read_block(ti.indirect, ptrs) == 0)
                for (uint32_t i = 0; i < YCFS_BLOCK_SIZE / 4; i++)
                    if (ptrs[i]) free_block_num(ptrs[i]);
            free_block_num(ti.indirect);
        }
    }
    free_inode_num(target_inode);

    txn_commit();
    return 0;
}

int ycfs_rename(const char* old_path, const char* new_path) {
    if (!initialized) return -1;
    uint32_t old_parent; char old_name[56];
    if (ycfs_resolve_parent(old_path, &old_parent, old_name) != 0) return -1;
    uint32_t new_parent; char new_name[56];
    if (ycfs_resolve_parent(new_path, &new_parent, new_name) != 0) return -1;

    uint32_t target_inode, target_type; uint64_t target_size;
    if (ycfs_lookup(old_parent, old_name, &target_inode, &target_type, &target_size) != 0) return -1;

    uint32_t existing_inode, existing_type; uint64_t existing_size;
    if (ycfs_lookup(new_parent, new_name, &existing_inode, &existing_type, &existing_size) == 0)
        return -1; /* destination already exists */

    txn_begin();
    if (append_dirent(new_parent, target_inode, new_name, target_type) != 0) { txn_commit(); return -1; }
    int r = clear_dirent(old_parent, old_name);
    txn_commit();
    return r;
}

int ycfs_stat(const char* path, uint32_t* size_out, uint8_t* is_dir_out) {
    if (!initialized) return -1;
    uint32_t inode, type; uint64_t size;
    if (ycfs_resolve(path, &inode, &type, &size) != 0) return -1;
    if (size_out)   *size_out   = (uint32_t)size;
    if (is_dir_out) *is_dir_out = (type == YCFS_TYPE_DIR) ? 1 : 0;
    return 0;
}

int ycfs_list_dir(const char* path, ycfs_entry_t* entries, int max_entries) {
    if (!initialized) return -1;
    uint32_t dir_inode, dir_type; uint64_t dir_size_unused;
    if (ycfs_resolve(path, &dir_inode, &dir_type, &dir_size_unused) != 0) return -1;
    if (dir_type != YCFS_TYPE_DIR) return -1;

    ycfs_inode_t dir;
    if (read_inode(dir_inode, &dir) != 0) return -1;

    int count = 0;
    static uint8_t dirbuf[YCFS_BLOCK_SIZE];
    uint64_t remaining = dir.size, offset = 0;
    while (remaining > 0 && count < max_entries) {
        uint32_t chunk = remaining > YCFS_BLOCK_SIZE ? YCFS_BLOCK_SIZE : (uint32_t)remaining;
        int64_t n = ycfs_read(dir_inode, offset, chunk, dirbuf);
        if (n <= 0) break;
        uint32_t entries_in_block = (uint32_t)n / sizeof(ycfs_dirent_t);
        ycfs_dirent_t* ents = (ycfs_dirent_t*)dirbuf;
        for (uint32_t i = 0; i < entries_in_block && count < max_entries; i++) {
            if (ents[i].inode == 0) continue;
            ycfs_inode_t child;
            if (read_inode(ents[i].inode, &child) != 0) continue;
            int k = 0; while (ents[i].name[k] && k < 31) { entries[count].name[k] = ents[i].name[k]; k++; }
            entries[count].name[k] = 0;
            entries[count].size   = (uint32_t)child.size;
            entries[count].is_dir = (child.mode == YCFS_TYPE_DIR) ? 1 : 0;
            count++;
        }
        offset    += n;
        remaining -= (uint64_t)n;
    }
    return count;
}

/* Create the file if it doesn't already exist (respecting the full path,
 * unlike sys_savefile's basename-only fat16_create fallback — this is
 * what lets YCFS actually honor subdirectories here), then overwrite its
 * content from offset 0. */
int64_t ycfs_savefile(const char* path, const void* buf, uint32_t size) {
    if (!initialized) return -1;
    uint32_t parent; char name[56];
    if (ycfs_resolve_parent(path, &parent, name) != 0) return -1;

    uint32_t inode, type; uint64_t existing_size;
    if (ycfs_lookup(parent, name, &inode, &type, &existing_size) != 0) {
        if (ycfs_create(parent, name, YCFS_TYPE_FILE, &inode) != 0) return -1;
    }
    return ycfs_write(inode, 0, size, buf);
}
