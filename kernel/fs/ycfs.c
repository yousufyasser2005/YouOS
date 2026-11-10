/*
 * ycfs.c — YCFS core driver (Phase 1: read-only)
 *
 * Directories are just files whose data is a stream of 64-byte
 * ycfs_dirent_t records — this is what gives real nested directory
 * trees, unlike FAT16's flat root. Free-space bitmaps and a self-
 * describing superblock (all layout offsets stored on-disk) mean the
 * driver never hardcodes geometry beyond what mkycfs.py wrote there.
 */

#include <kernel/sync.h>
#include <kernel/ycfs.h>
#include <kernel/session.h>
#include <kernel/ata.h>
#include <kernel/vga.h>

static ycfs_superblock_t sb;
static int initialized = 0;
static void ycfs_journal_replay(void);

#define SECTORS_PER_BLOCK (YCFS_BLOCK_SIZE / 512)
#define RDRUN_BLOCKS 31          /* blocks per read command: 31 * 8 = 248 sectors (an ATA command moves at most 255) */

/* Pointers per pointer-block, and bits per bitmap block. */
#define PPB          (YCFS_BLOCK_SIZE / 4)
#define BITS_PER_BM  (YCFS_BLOCK_SIZE * 8)

/* 64-bit LBA math: a block number can reach 2^32 (16TB) and the sector
 * index of block N is N*8, which overflows 32 bits from 2TB up. */
static int read_block(uint32_t block_num, void* buf) {
    uint64_t lba = (uint64_t)YCFS_START_LBA + (uint64_t)block_num * SECTORS_PER_BLOCK;
    return ata_read_sectors(lba, (uint8_t)SECTORS_PER_BLOCK, buf);
}

static uint32_t bbm_blocks(void) { return sb.block_bitmap_blocks ? sb.block_bitmap_blocks : 1; }
static uint32_t ibm_blocks(void) { return sb.inode_bitmap_blocks ? sb.inode_bitmap_blocks : 1; }

/* Cache of the two pointer blocks a block lookup walks (slot 0: the
 * indirect block, or the double-indirect root; slot 1: the leaf pointer
 * block under it). Without this every data block of a large file costs
 * up to two extra disk reads. A slot is valid when blk != 0 (block 0 is
 * the superblock, never a pointer block). All writes to pointer blocks go
 * through ps_store()/ps_create(), which keep the cache coherent. */
typedef struct { uint32_t blk; int dirty; uint32_t p[PPB]; } ptrslot_t;
static ptrslot_t ps[2];
/* While ycfs_write() runs it sets defer_ptr: updates to a pointer block are
 * then kept in the cache (dirty) and written once, when the slot is reused
 * or the call ends, instead of after every single new data block. */
static int defer_ptr = 0;
static int ps_store(int slot);
static int ps_flush_all(void) {
    int rc = 0;
    for (int i = 0; i < 2; i++)
        if (ps[i].blk && ps[i].dirty && ps_store(i) != 0) rc = -1;
    return rc;
}
static void ps_invalidate(void) {
    ps_flush_all();
    ps[0].blk = 0; ps[1].blk = 0; ps[0].dirty = 0; ps[1].dirty = 0;
}
static int ps_load(int slot, uint32_t blk) {
    if (ps[slot].blk == blk) return 0;
    if (ps[slot].blk && ps[slot].dirty && ps_store(slot) != 0) return -1;
    ps[slot].blk = 0; ps[slot].dirty = 0;
    if (read_block(blk, ps[slot].p) != 0) return -1;
    ps[slot].blk = blk;
    return 0;
}
/* Logical block `idx` of `inode` -> physical block. alloc=0: a hole
 * yields *out = 0 and success; alloc=1 allocates what is missing. */
static int bmap(ycfs_inode_t* inode, uint32_t idx, int alloc, uint32_t* out);

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
    if (s->version != 1 && s->version != 2) {
        vga_puts_color("  [!!] YCFS: unsupported format version\n", VGA_LIGHT_RED, VGA_BLACK);
        return -1;
    }
    if (s->block_size != YCFS_BLOCK_SIZE) {
        vga_puts_color("  [!!] YCFS: unsupported block size\n", VGA_LIGHT_RED, VGA_BLACK);
        return -1;
    }
    sb = *s;
    /* The bitmaps must be long enough to cover every block/inode, or the
     * allocator would index past them. */
    if ((uint64_t)bbm_blocks() * BITS_PER_BM < sb.total_blocks ||
        (uint64_t)ibm_blocks() * BITS_PER_BM < sb.total_inodes) {
        vga_puts_color("  [!!] YCFS: bitmap too short for geometry\n", VGA_LIGHT_RED, VGA_BLACK);
        return -1;
    }
    ps_invalidate();
    initialized = 1;
    ycfs_journal_replay();
    vga_puts_color("  [OK] YCFS filesystem mounted\n", VGA_LIGHT_GREEN, VGA_BLACK);
    return 0;
}

uint32_t ycfs_root_inode(void) {
    return sb.root_inode;
}

/* Read `size` bytes starting at `offset` from the given inode's data,
 * walking direct, indirect and double-indirect pointers. A hole (zero
 * pointer) ends the read early, same as before. */
static int64_t ycfs_read_impl(uint32_t inode_num, uint64_t offset, uint32_t size, void* buf) {
    if (!initialized) return -1;
    ycfs_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) return -1;

    uint8_t* dst = (uint8_t*)buf;
    uint32_t bytes_read = 0;
    static uint8_t blockbuf[YCFS_BLOCK_SIZE];

    while (bytes_read < size && offset < inode.size) {
        uint32_t block_idx = (uint32_t)(offset / YCFS_BLOCK_SIZE);
        uint32_t block_off = (uint32_t)(offset % YCFS_BLOCK_SIZE);
        uint32_t phys_block;

        if (bmap(&inode, block_idx, 0, &phys_block) != 0) break;
        if (!phys_block) break;

        /* Whole blocks: read a run of physically contiguous blocks (up to 31 = 248 sectors) with ONE
         * disk command, straight into the caller's buffer. */
        if (block_off == 0 && size - bytes_read >= YCFS_BLOCK_SIZE && inode.size - offset >= YCFS_BLOCK_SIZE) {
            uint32_t want_blocks = (size - bytes_read) / YCFS_BLOCK_SIZE;
            uint64_t file_blocks = (inode.size - offset) / YCFS_BLOCK_SIZE;
            if (want_blocks > RDRUN_BLOCKS) want_blocks = RDRUN_BLOCKS;
            if ((uint64_t)want_blocks > file_blocks) want_blocks = (uint32_t)file_blocks;
            uint32_t run = 1;
            while (run < want_blocks) {
                uint32_t nxt;
                if (bmap(&inode, block_idx + run, 0, &nxt) != 0 || nxt != phys_block + run) break;
                run++;
            }
            uint64_t lba = (uint64_t)YCFS_START_LBA + (uint64_t)phys_block * SECTORS_PER_BLOCK;
            if (ata_read_sectors(lba, (uint8_t)(run * SECTORS_PER_BLOCK), dst + bytes_read) != 0) break;
            bytes_read += run * YCFS_BLOCK_SIZE;
            offset     += (uint64_t)run * YCFS_BLOCK_SIZE;
            continue;
        }

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
static int ycfs_lookup_impl(uint32_t dir_inode, const char* name,
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
    uint64_t lba = (uint64_t)YCFS_START_LBA + (uint64_t)block_num * SECTORS_PER_BLOCK;
    return ata_write_sectors(lba, (uint8_t)SECTORS_PER_BLOCK, buf);
}

/* Write `count` consecutive blocks starting at `start`. One ATA command (and
 * one drive-cache flush) per up-to-31 blocks instead of one per block:
 * 31 blocks = 248 sectors, the most a single ATA command can carry (255). */
#define WRUN_BLOCKS 31
/* Plain file data: written without a cache flush after each command. The driver flushes before the
 * next ordinary write, and every ycfs_write ends with the inode write, so the data is on the disk
 * before the metadata that refers to it, and flushed before the call returns. */
static int raw_write_data_blocks(uint32_t start, uint32_t count, const void* buf) {
    const uint8_t* p = (const uint8_t*)buf;
    while (count) {
        uint32_t n = count > WRUN_BLOCKS ? WRUN_BLOCKS : count;
        uint64_t lba = (uint64_t)YCFS_START_LBA + (uint64_t)start * SECTORS_PER_BLOCK;
        if (ata_write_sectors_nf(lba, (uint8_t)(n * SECTORS_PER_BLOCK), p) != 0) return -1;
        start += n; count -= n; p += (uint64_t)n * YCFS_BLOCK_SIZE;
    }
    return 0;
}
static int raw_write_blocks(uint32_t start, uint32_t count, const void* buf) {
    const uint8_t* p = (const uint8_t*)buf;
    while (count) {
        uint32_t n = count > WRUN_BLOCKS ? WRUN_BLOCKS : count;
        uint64_t lba = (uint64_t)YCFS_START_LBA + (uint64_t)start * SECTORS_PER_BLOCK;
        if (ata_write_sectors(lba, (uint8_t)(n * SECTORS_PER_BLOCK), p) != 0) return -1;
        start += n; count -= n; p += (uint64_t)n * YCFS_BLOCK_SIZE;
    }
    return 0;
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
/* Set by journal_log()/txn_commit() whenever journal_pos wraps back to 0
 * mid-transaction (i.e. this transaction's own writes filled the journal
 * region and looped). When set, a transaction's entries are NOT a single
 * contiguous [0, journal_pos) range, so txn_commit()'s wipe must fall
 * back to clearing the whole journal region instead of just that range.
 * Reset at the start of every transaction in txn_begin(). */
static int txn_wrapped = 0;

static int journal_log(uint32_t txn_id, uint32_t target_block, const void* data) {
    if (sb.journal_num_blocks == 0) return -1; /* unformatted / no journal */
    if (journal_pos + 2 > sb.journal_num_blocks) { txn_wrapped = 1; journal_pos = 0; }
    /* Descriptor + logged data go out as ONE two-block write (same on-disk
     * layout as before: descriptor block, then the data block). */
    static uint8_t jbuf[2 * YCFS_BLOCK_SIZE];
    for (uint32_t i = 0; i < YCFS_BLOCK_SIZE; i++) jbuf[i] = 0;
    ycfs_journal_hdr_t* hdr = (ycfs_journal_hdr_t*)jbuf;
    hdr->magic        = YCFS_JOURNAL_MAGIC;
    hdr->txn_id       = txn_id;
    hdr->target_block = target_block;
    hdr->type         = YCFS_JTYPE_DESC;
    const uint8_t* d = (const uint8_t*)data;
    for (uint32_t i = 0; i < YCFS_BLOCK_SIZE; i++) jbuf[YCFS_BLOCK_SIZE + i] = d[i];
    raw_write_blocks(sb.journal_start_block + journal_pos, 2, jbuf);
    journal_pos += 2;
    return 0;
}

static uint32_t txn_begin(void) {
    current_txn_id = next_txn_id++;
    txn_wrapped = 0;
    return current_txn_id;
}

static void txn_commit(void) {
    if (current_txn_id == 0) return;
    if (sb.journal_num_blocks != 0) {
        if (journal_pos + 1 > sb.journal_num_blocks) { txn_wrapped = 1; journal_pos = 0; }
        ycfs_journal_hdr_t hdr;
        uint8_t* hp = (uint8_t*)&hdr;
        for (uint32_t i = 0; i < sizeof(hdr); i++) hp[i] = 0;
        hdr.magic  = YCFS_JOURNAL_MAGIC;
        hdr.txn_id = current_txn_id;
        hdr.type   = YCFS_JTYPE_COMMIT;
        raw_write_block(sb.journal_start_block + journal_pos, &hdr);
        journal_pos += 1;
        uint32_t commit_pos = journal_pos; /* this txn's entries span
                                             * [0, commit_pos) UNLESS
                                             * txn_wrapped is set, in which
                                             * case that range isn't
                                             * reliable and we fall back
                                             * to a full-region wipe below */

        /* Wipe the journal immediately -- confirmed bug fix, 2026-08-29.
         * write_block() logs to the journal THEN performs the real,
         * synchronous raw_write_block() in the same call (write-ahead
         * logging here logs before the real write, it does not defer
         * it) -- so by the time this function returns, every write made
         * during this transaction is already fully durable on disk. The
         * journal entries serve no further purpose after that point.
         *
         * Leaving them in place until the NEXT boot's
         * ycfs_journal_replay() is what caused the bug: replay
         * unconditionally re-applies every committed transaction it
         * finds, with no way to know a target block has been legitimately
         * updated again since. ycfs_create()'s own transaction (blank
         * inode + new dirent) would sit here un-cleared for the rest of
         * the session; a subsequent ycfs_write() call updates that same
         * inode with the real file content -- correctly, but NOT inside
         * a transaction, so the journal never learns about it. At the
         * next boot, replay would blindly restore the stale blank inode
         * from the old transaction, stomping the real content right back
         * to size=0/no data block -- confirmed via direct byte-level
         * inspection of disk.img (correct inode present pre-reboot,
         * reverted to blank post-reboot, with the exact stale DESC/COMMIT
         * records for this transaction still sitting in the journal
         * beforehand). Clearing the journal here, right after everything
         * in it has already been durably applied, removes that window
         * entirely -- mirrors the same zero-fill idiom
         * ycfs_journal_replay() already uses at its own end. */
        static uint8_t zeros[WRUN_BLOCKS * YCFS_BLOCK_SIZE];   /* .bss: already zero */
        uint32_t wipe_count = txn_wrapped ? sb.journal_num_blocks : commit_pos;
        for (uint32_t i = 0; i < wipe_count; i += WRUN_BLOCKS) {
            uint32_t n = wipe_count - i > WRUN_BLOCKS ? WRUN_BLOCKS : wipe_count - i;
            raw_write_blocks(sb.journal_start_block + i, n, zeros);
        }
        journal_pos = 0;
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

    /* Wipe the whole journal region (one command per 31 blocks, not per block). */
    {
        static uint8_t zeros[WRUN_BLOCKS * YCFS_BLOCK_SIZE];      /* .bss: already zero */
        for (uint32_t i = 0; i < sb.journal_num_blocks; i += WRUN_BLOCKS) {
            uint32_t n = sb.journal_num_blocks - i > WRUN_BLOCKS ? WRUN_BLOCKS : sb.journal_num_blocks - i;
            raw_write_blocks(sb.journal_start_block + i, n, zeros);
        }
    }
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
static int ycfs_journal_self_test_impl(void) {
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

/* ---- bitmap allocator ----
 * Bitmaps can span many blocks (a 1TB disk has 32MB of block bitmap), so
 * one block at a time is cached. Allocation writes through immediately
 * (a crash must never leave a used block marked free); freeing is
 * deferred -- the caller batches many frees and calls free_flush() --
 * since a crash mid-free only leaks blocks, which is harmless. */
static uint8_t  bm_buf[YCFS_BLOCK_SIZE];
static uint32_t bm_blk   = 0xFFFFFFFFu;   /* which block bm_buf holds */
static int      bm_dirty = 0;
static int      sb_dirty = 0;             /* superblock counters need writing */

static int bm_flush(void) {
    if (bm_blk != 0xFFFFFFFFu && bm_dirty) {
        bm_dirty = 0;
        return write_block(bm_blk, bm_buf);
    }
    return 0;
}
static int bm_load(uint32_t blk) {
    if (bm_blk == blk) return 0;
    if (bm_flush() != 0) return -1;
    bm_blk = 0xFFFFFFFFu;
    if (read_block(blk, bm_buf) != 0) return -1;
    bm_blk = blk;
    return 0;
}

/* Find and claim a free bit, searching forward from `hint` and wrapping
 * once. Caller updates the relevant sb.free_* counter and persists the
 * superblock. Skips fully-used 64-bit words, so a mostly-full bitmap is
 * scanned quickly. */
static int alloc_bit(uint32_t bitmap_start, uint32_t nbm, uint32_t total_bits,
                     uint32_t hint, uint32_t* out_idx, int flush) {
    uint32_t nblocks = (uint32_t)(((uint64_t)total_bits + BITS_PER_BM - 1) / BITS_PER_BM);
    if (nblocks > nbm) nblocks = nbm;
    if (hint >= total_bits) hint = 0;
    uint32_t start_blk = hint / BITS_PER_BM;

    for (uint32_t k = 0; k <= nblocks; k++) {
        uint32_t bi = (start_blk + k) % nblocks;
        if (bm_load(bitmap_start + bi) != 0) return -1;
        uint32_t from  = (k == 0) ? (hint % BITS_PER_BM) : 0;
        uint64_t base  = (uint64_t)bi * BITS_PER_BM;
        uint32_t limit = BITS_PER_BM;
        if (base + limit > total_bits) limit = (uint32_t)(total_bits - base);
        uint32_t i = from;
        while (i < limit) {
            if ((i & 63) == 0 && i + 64 <= limit &&
                *(uint64_t*)(bm_buf + (i >> 3)) == ~0ull) { i += 64; continue; }
            if (!(bm_buf[i >> 3] & (1u << (i & 7)))) {
                bm_buf[i >> 3] |= (uint8_t)(1u << (i & 7));
                bm_dirty = 1;
                if (flush && bm_flush() != 0) return -1;
                *out_idx = (uint32_t)(base + i);
                return 0;
            }
            i++;
        }
    }
    return -1; /* full */
}

static int free_bit(uint32_t bitmap_start, uint32_t idx) {
    if (bm_load(bitmap_start + idx / BITS_PER_BM) != 0) return -1;
    bm_buf[(idx % BITS_PER_BM) >> 3] &= (uint8_t)~(1u << (idx & 7));
    bm_dirty = 1;
    return 0;
}

/* ---- batch block reservation ----
 * Allocating blocks one at a time costs a bitmap write and a superblock
 * write EACH (and every disk write also costs a drive-cache flush). A write
 * of many blocks instead claims them up front: bits are set in the cached
 * bitmap block (written once per bitmap block touched) and the superblock
 * counters are written once. alloc_block_num() then hands the claimed
 * blocks out one by one; pool_release() gives back any that were not
 * needed. A crash in between can only leak blocks, never double-use them. */
#define POOL_MAX 256   /* one 1MB write can claim all its blocks in a single bitmap write */
static uint32_t pool[POOL_MAX];
static uint32_t pool_n = 0, pool_i = 0;
static uint32_t pool_want = 0;          /* blocks the running write still expects to need */
static int pool_fill(uint32_t n);       /* defined after the free helpers */
static void pool_release(void);

static int alloc_block_num(uint32_t* out_block) {
    if (pool_i < pool_n) { *out_block = pool[pool_i++]; return 0; }
    if (pool_want > 0) {
        if (pool_fill(pool_want) == 0) {
            pool_want -= (pool_n < pool_want) ? pool_n : pool_want;
            *out_block = pool[pool_i++];
            return 0;
        }
    }
    if (sb.free_blocks == 0) return -1;
    if (alloc_bit(sb.block_bitmap_block, bbm_blocks(), sb.total_blocks,
                  sb.block_hint, out_block, 1) != 0) return -1;
    sb.block_hint = *out_block + 1;
    sb.free_blocks--;
    write_superblock();
    return 0;
}

static int alloc_inode_num(uint32_t* out_inode) {
    if (sb.free_inodes == 0) return -1;
    if (alloc_bit(sb.inode_bitmap_block, ibm_blocks(), sb.total_inodes,
                  sb.inode_hint, out_inode, 1) != 0) return -1;
    sb.inode_hint = *out_inode + 1;
    sb.free_inodes--;
    write_superblock();
    return 0;
}

/* Allocate a fresh, zeroed pointer block into cache slot `slot`. */
static int ps_create(int slot, uint32_t* out_blk, ycfs_inode_t* inode) {
    uint32_t nb;
    if (alloc_block_num(&nb) != 0) return -1;
    if (ps[slot].blk && ps[slot].dirty) ps_store(slot);
    ps[slot].blk = 0; ps[slot].dirty = 0;
    for (uint32_t i = 0; i < PPB; i++) ps[slot].p[i] = 0;
    if (write_block(nb, ps[slot].p) != 0) return -1;
    ps[slot].blk = nb;
    inode->blocks_used++;
    *out_blk = nb;
    return 0;
}
static int ps_store(int slot) {
    int rc = write_block(ps[slot].blk, ps[slot].p);
    ps[slot].dirty = 0;
    return rc;
}
/* Persist a change to a cached pointer block now, or note it for later. */
static int ps_commit(int slot) {
    if (defer_ptr) { ps[slot].dirty = 1; return 0; }
    return ps_store(slot);
}

/* Return the physical block backing logical block `idx` of `inode`,
 * walking 12 direct pointers, then the single-indirect block (1024
 * entries), then the double-indirect block (1024 x 1024 entries) --
 * ~4GB per file. With alloc=1, missing data and pointer blocks are
 * allocated and inode->direct/indirect/dindirect/blocks_used updated
 * (the caller persists the inode). With alloc=0 a hole is reported as
 * *out = 0 with a success return. */
static int bmap(ycfs_inode_t* inode, uint32_t idx, int alloc, uint32_t* out) {
    uint32_t nb;
    if (idx < YCFS_DIRECT_BLOCKS) {
        if (inode->direct[idx] == 0) {
            if (!alloc) { *out = 0; return 0; }
            if (alloc_block_num(&nb) != 0) return -1;
            inode->direct[idx] = nb;
            inode->blocks_used++;
        }
        *out = inode->direct[idx];
        return 0;
    }
    idx -= YCFS_DIRECT_BLOCKS;

    if (idx < PPB) {                                   /* single indirect */
        if (inode->indirect == 0) {
            if (!alloc) { *out = 0; return 0; }
            if (ps_create(0, &nb, inode) != 0) return -1;
            inode->indirect = nb;
        } else if (ps_load(0, inode->indirect) != 0) return -1;
        uint32_t v = ps[0].p[idx];
        if (v == 0) {
            if (!alloc) { *out = 0; return 0; }
            if (alloc_block_num(&nb) != 0) return -1;
            ps[0].p[idx] = nb;
            inode->blocks_used++;
            if (ps_commit(0) != 0) return -1;
            v = nb;
        }
        *out = v;
        return 0;
    }
    idx -= PPB;

    if (idx >= (uint64_t)PPB * PPB) return -1;         /* beyond ~4GB */
    uint32_t l1 = idx / PPB, l2 = idx % PPB;
    if (inode->dindirect == 0) {                       /* double indirect */
        if (!alloc) { *out = 0; return 0; }
        if (ps_create(0, &nb, inode) != 0) return -1;
        inode->dindirect = nb;
    } else if (ps_load(0, inode->dindirect) != 0) return -1;
    uint32_t leaf = ps[0].p[l1];
    if (leaf == 0) {
        if (!alloc) { *out = 0; return 0; }
        if (ps_create(1, &leaf, inode) != 0) return -1;
        ps[0].p[l1] = leaf;
        if (ps_commit(0) != 0) return -1;
    } else if (ps_load(1, leaf) != 0) return -1;
    uint32_t v = ps[1].p[l2];
    if (v == 0) {
        if (!alloc) { *out = 0; return 0; }
        if (alloc_block_num(&nb) != 0) return -1;
        ps[1].p[l2] = nb;
        inode->blocks_used++;
        if (ps_commit(1) != 0) return -1;
        v = nb;
    }
    *out = v;
    return 0;
}

/* Write `size` bytes at `offset` into inode_num's data, allocating new
 * blocks as needed and growing inode.size if the write extends past the
 * current end of file. Used for regular file writes AND for appending
 * dirents to a directory's own byte stream (a directory is just a file). */
/* How many NEW pointer blocks (single-indirect, double-indirect root, and
 * double-indirect leaves) writing logical blocks [first, last] will need.
 * Counting them exactly lets ycfs_write() reserve precisely what it will
 * use, so nothing has to be handed back (handing back costs two more
 * disk writes per call). */
static uint32_t ptr_blocks_needed(ycfs_inode_t* inode, uint32_t first, uint32_t last) {
    uint32_t n = 0;
    uint32_t i0 = YCFS_DIRECT_BLOCKS, i1 = YCFS_DIRECT_BLOCKS + PPB;
    if (last >= i0 && first < i1 && inode->indirect == 0) n++;
    if (last >= i1) {
        if (inode->dindirect == 0) n++;
        uint32_t a = (first > i1 ? first : i1) - i1;
        uint32_t b = last - i1;
        uint32_t l1a = a / PPB, l1b = b / PPB;
        if (l1b >= PPB) l1b = PPB - 1;
        for (uint32_t l1 = l1a; l1 <= l1b; l1++) {
            if (inode->dindirect == 0) { n++; continue; }
            if (ps_load(0, inode->dindirect) != 0) return n;
            if (ps[0].p[l1] == 0) n++;
        }
    }
    return n;
}

static int64_t ycfs_write_impl(uint32_t inode_num, uint64_t offset, uint32_t size, const void* buf) {
    if (!initialized) return -1;
    ycfs_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) return -1;

    const uint8_t* src = (const uint8_t*)buf;
    uint32_t bytes_written = 0;
    static uint8_t wbuf[WRUN_BLOCKS * YCFS_BLOCK_SIZE];

    /* Reserve every block this write will need -- data and pointer blocks,
     * counted exactly -- with one bitmap write instead of a bitmap and a
     * superblock write per block. */
    if (size > 0) {
        uint32_t first = (uint32_t)(offset / YCFS_BLOCK_SIZE);
        uint32_t last  = (uint32_t)((offset + size - 1) / YCFS_BLOCK_SIZE);
        uint32_t need = 0;
        for (uint32_t idx = first; idx <= last; idx++) {
            uint32_t p;
            if (bmap(&inode, idx, 0, &p) == 0 && p == 0) need++;
        }
        uint32_t extra = need ? ptr_blocks_needed(&inode, first, last) : 0;
        pool_want = need ? need + extra : 0;
    }
    defer_ptr = 1;   /* pointer-block updates are written once, below */

    int failed = 0;
    while (bytes_written < size && !failed) {
        /* Stage a run of physically contiguous blocks, then write the whole
         * run with one command. */
        uint32_t run = 0, run_start = 0, staged = 0;
        while (bytes_written + staged < size && run < WRUN_BLOCKS) {
            uint64_t pos       = offset + bytes_written + staged;
            uint32_t block_idx = (uint32_t)(pos / YCFS_BLOCK_SIZE);
            uint32_t block_off = (uint32_t)(pos % YCFS_BLOCK_SIZE);
            uint32_t phys;
            if (bmap(&inode, block_idx, 1, &phys) != 0) { failed = 1; break; }
            if (run && phys != run_start + run) break;        /* run ends here; this block starts the next one */
            if (!run) run_start = phys;

            uint32_t chunk = YCFS_BLOCK_SIZE - block_off;
            uint32_t remaining = size - bytes_written - staged;
            if (chunk > remaining) chunk = remaining;

            uint8_t* blk = wbuf + (uint64_t)run * YCFS_BLOCK_SIZE;
            if (block_off != 0 || chunk != YCFS_BLOCK_SIZE) {
                if (read_block(phys, blk) != 0) { failed = 1; break; }
            }
            for (uint32_t k = 0; k < chunk; k++) blk[block_off + k] = src[bytes_written + staged + k];
            run++;
            staged += chunk;
        }
        if (run == 0) break;

        int werr = 0;
        if (current_txn_id != 0) {                      /* inside a transaction: every block is journaled */
            for (uint32_t i = 0; i < run && !werr; i++)
                if (write_block(run_start + i, wbuf + (uint64_t)i * YCFS_BLOCK_SIZE) != 0) werr = 1;
        } else {
            if (raw_write_data_blocks(run_start, run, wbuf) != 0) werr = 1;
        }
        if (werr) { failed = 1; break; }
        bytes_written += staged;
    }

    if (ps_flush_all() != 0) failed = 1;
    defer_ptr = 0;
    pool_release();                                     /* return blocks reserved but not used */

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
static int ycfs_create_impl(uint32_t dir_inode, const char* name, uint32_t type, uint32_t* out_inode) {
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
    /* Owner/permission defaults — mirrors mkycfs.py's pack_inode defaults
     * (0755 dirs / 0644 files) and assigns ownership to whoever is
     * creating the file. Unknown session (pre-login bootstrap, e.g.
     * first-time auth.dat creation) defaults to root ownership, matching
     * how the seeded filesystem itself is entirely root-owned. */
    uint32_t creator_uid = YCFS_ROOT_UID, creator_gid = YCFS_ROOT_GID;
    session_lookup(&creator_uid, &creator_gid); /* leaves root defaults on failure */
    ni.uid  = creator_uid;
    ni.gid  = creator_gid;
    ni.perm = (type == YCFS_TYPE_DIR) ? 0755 : 0644;
    if (write_inode(new_inode_num, &ni) != 0) { txn_commit(); return -1; }

    if (append_dirent(dir_inode, new_inode_num, name, type) != 0) { txn_commit(); return -1; }

    txn_commit();
    if (out_inode) *out_inode = new_inode_num;
    return 0;
}

/* ==================== Phase 2: unlink / mkdir / rename / list ==================== */

/* Deferred frees: mark in the cached bitmap and bump the in-memory
 * counters; free_flush() persists the bitmap block and superblock. */
static void free_block_num(uint32_t block_num) {
    if (block_num < sb.data_start_block || block_num >= sb.total_blocks) return; /* corrupt pointer */
    if (free_bit(sb.block_bitmap_block, block_num) == 0) {
        sb.free_blocks++;
        if (block_num < sb.block_hint) sb.block_hint = block_num;
        sb_dirty = 1;
    }
}

static void free_inode_num(uint32_t inode_num) {
    if (inode_num == 0 || inode_num >= sb.total_inodes) return;
    if (free_bit(sb.inode_bitmap_block, inode_num) == 0) {
        sb.free_inodes++;
        if (inode_num < sb.inode_hint) sb.inode_hint = inode_num;
        sb_dirty = 1;
    }
}

static void free_flush(void) {
    bm_flush();
    if (sb_dirty) { write_superblock(); sb_dirty = 0; }
}

/* Claim up to n free blocks in one go (see the pool comment above). */
static int pool_fill(uint32_t n) {
    if (n > POOL_MAX) n = POOL_MAX;
    if (n > sb.free_blocks) n = sb.free_blocks;
    pool_n = pool_i = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t b;
        if (alloc_bit(sb.block_bitmap_block, bbm_blocks(), sb.total_blocks,
                      sb.block_hint, &b, 0) != 0) break;
        pool[pool_n++] = b;
        sb.block_hint = b + 1;
    }
    if (bm_flush() != 0) { pool_n = pool_i = 0; return -1; }
    if (pool_n == 0) return -1;
    sb.free_blocks -= pool_n;
    write_superblock();
    return 0;
}

/* Give back claimed blocks that the write did not end up using. */
static void pool_release(void) {
    for (uint32_t i = pool_i; i < pool_n; i++) free_block_num(pool[i]);
    pool_n = pool_i = 0;
    pool_want = 0;
    free_flush();
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
static int ycfs_perm_check(uint32_t inode_uid, uint32_t inode_gid,
                            uint16_t perm, int want) {
    uint32_t uid, gid;
    if (session_lookup(&uid, &gid) != 0) {
        /* No session recorded for this CR3 — either pre-login bootstrap
         * code (e.g. first-time account setup, which must be able to
         * write auth.dat before any login has happened) or a sys_exec'd
         * process like shell/cat that never calls sys_set_session_uid.
         * Since there's no code-signing/capability boundary yet (see
         * session.h), restricting unknown sessions doesn't add real
         * security — it only breaks legitimate pre-login writes. Default
         * to root-equivalent instead, matching this OS's behavior before
         * stage 3 existed (no checks at all) for anything that hasn't
         * explicitly logged in. */
        return 1;
    }
    if (uid == YCFS_ROOT_UID) return 1; /* root bypasses all checks */
    if (session_is_elevated()) return 1; /* youdo — session-scoped elevation */
    if (uid == inode_uid) return (perm & (want << YCFS_PERM_OWNER_SHIFT)) != 0;
    if (gid == inode_gid) return (perm & (want << YCFS_PERM_GROUP_SHIFT)) != 0;
    return (perm & want) != 0;
}

static int ycfs_check_access_impl(uint32_t inode_num, int want) {
    ycfs_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) return 0;
    return ycfs_perm_check(inode.uid, inode.gid, inode.perm, want);
}

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

static int ycfs_mkdir_impl(const char* path) {
    if (!initialized) return -1;
    uint32_t parent; char name[56];
    if (ycfs_resolve_parent(path, &parent, name) != 0) return -1;
    if (!ycfs_check_access(parent, YCFS_WANT_W)) return -1;
    uint32_t new_inode;
    return ycfs_create(parent, name, YCFS_TYPE_DIR, &new_inode);
}

/* ---- shrinking files ---- */

/* Free every data block of `inode` at logical index >= keep, and every
 * pointer block that ends up empty. Adjusts blocks_used; the caller
 * persists the inode and calls free_flush(). */
static void trunc_blocks(ycfs_inode_t* inode, uint32_t keep) {
    static uint32_t top[PPB], leaf[PPB];

    for (uint32_t i = keep; i < YCFS_DIRECT_BLOCKS; i++) {
        if (inode->direct[i]) {
            free_block_num(inode->direct[i]);
            inode->direct[i] = 0;
            if (inode->blocks_used) inode->blocks_used--;
        }
    }

    if (inode->indirect) {
        uint32_t first = keep > YCFS_DIRECT_BLOCKS ? keep - YCFS_DIRECT_BLOCKS : 0;
        if (read_block(inode->indirect, top) == 0) {
            int dirty = 0, left = 0;
            for (uint32_t j = 0; j < PPB; j++) {
                if (!top[j]) continue;
                if (j >= first) {
                    free_block_num(top[j]); top[j] = 0; dirty = 1;
                    if (inode->blocks_used) inode->blocks_used--;
                } else left = 1;
            }
            if (!left) {
                free_block_num(inode->indirect); inode->indirect = 0;
                if (inode->blocks_used) inode->blocks_used--;
            } else if (dirty) write_block(inode->indirect, top);
        }
    }

    if (inode->dindirect) {
        uint32_t base  = YCFS_DIRECT_BLOCKS + PPB;
        uint32_t first = keep > base ? keep - base : 0;
        if (read_block(inode->dindirect, top) == 0) {
            int rdirty = 0, rleft = 0;
            for (uint32_t a = 0; a < PPB; a++) {
                if (!top[a]) continue;
                uint32_t a_start = a * PPB;
                if (a_start + PPB <= first) { rleft = 1; continue; }   /* wholly kept */
                if (read_block(top[a], leaf) != 0) { rleft = 1; continue; }
                uint32_t lstart = first > a_start ? first - a_start : 0;
                int ldirty = 0, lleft = 0;
                for (uint32_t b = 0; b < PPB; b++) {
                    if (!leaf[b]) continue;
                    if (b >= lstart) {
                        free_block_num(leaf[b]); leaf[b] = 0; ldirty = 1;
                        if (inode->blocks_used) inode->blocks_used--;
                    } else lleft = 1;
                }
                if (!lleft) {
                    free_block_num(top[a]); top[a] = 0; rdirty = 1;
                    if (inode->blocks_used) inode->blocks_used--;
                } else {
                    rleft = 1;
                    if (ldirty) write_block(top[a], leaf);
                }
            }
            if (!rleft) {
                free_block_num(inode->dindirect); inode->dindirect = 0;
                if (inode->blocks_used) inode->blocks_used--;
            } else if (rdirty) write_block(inode->dindirect, top);
        }
    }
    ps_invalidate();   /* freed pointer blocks must not be served from cache */
}

/* Cut a file down to new_size bytes (no-op if it is not larger). */
static int ycfs_truncate(uint32_t inode_num, uint64_t new_size) {
    ycfs_inode_t inode;
    if (read_inode(inode_num, &inode) != 0) return -1;
    if (new_size >= inode.size) return 0;

    uint32_t keep = (uint32_t)((new_size + YCFS_BLOCK_SIZE - 1) / YCFS_BLOCK_SIZE);
    uint32_t rem  = (uint32_t)(new_size % YCFS_BLOCK_SIZE);
    if (rem) {
        /* Zero the stale tail of the last kept block so a later write past
         * the end can never expose old data. */
        uint32_t phys;
        static uint8_t tb[YCFS_BLOCK_SIZE];
        if (bmap(&inode, keep - 1, 0, &phys) == 0 && phys && read_block(phys, tb) == 0) {
            for (uint32_t i = rem; i < YCFS_BLOCK_SIZE; i++) tb[i] = 0;
            write_block(phys, tb);
        }
    }
    inode.size = new_size;
    trunc_blocks(&inode, keep);
    if (write_inode(inode_num, &inode) != 0) return -1;
    free_flush();
    return 0;
}

/* 1 if the directory has no live entries. */
static int dir_is_empty(uint32_t dir_inode) {
    ycfs_inode_t dir;
    if (read_inode(dir_inode, &dir) != 0) return 0;
    static uint8_t eb[YCFS_BLOCK_SIZE];
    uint64_t off = 0;
    while (off < dir.size) {
        uint32_t chunk = dir.size - off > YCFS_BLOCK_SIZE ? YCFS_BLOCK_SIZE : (uint32_t)(dir.size - off);
        int64_t n = ycfs_read(dir_inode, off, chunk, eb);
        if (n <= 0) break;
        for (uint32_t i = 0; i + sizeof(ycfs_dirent_t) <= (uint32_t)n; i += sizeof(ycfs_dirent_t))
            if (((ycfs_dirent_t*)(eb + i))->inode != 0) return 0;
        off += (uint64_t)n;
    }
    return 1;
}

static int ycfs_unlink_impl(const char* path) {
    if (!initialized) return -1;
    uint32_t parent; char name[56];
    if (ycfs_resolve_parent(path, &parent, name) != 0) return -1;
    if (!ycfs_check_access(parent, YCFS_WANT_W)) return -1;

    uint32_t target_inode, target_type; uint64_t target_size;
    if (ycfs_lookup(parent, name, &target_inode, &target_type, &target_size) != 0) return -1;

    /* Never orphan a directory's contents: removing a non-empty directory
     * must be done child by child (the file manager does this). */
    if (target_type == YCFS_TYPE_DIR && !dir_is_empty(target_inode)) return -1;

    /* Phase 1 (journaled): remove the directory entry and release the
     * inode. After this the file is gone as far as the namespace goes. */
    ycfs_inode_t ti;
    int have_inode = (read_inode(target_inode, &ti) == 0);

    txn_begin();
    if (clear_dirent(parent, name) != 0) { txn_commit(); return -1; }
    free_inode_num(target_inode);
    free_flush();
    txn_commit();

    /* Phase 2 (not journaled): release the data and pointer blocks. A
     * crash in here leaks blocks but cannot corrupt anything, and doing
     * it outside the journal matters -- a multi-GB file has hundreds of
     * thousands of blocks, far more than the journal region can log. */
    if (have_inode) {
        static uint32_t l1[PPB], l2[PPB];
        for (int i = 0; i < YCFS_DIRECT_BLOCKS; i++)
            if (ti.direct[i]) free_block_num(ti.direct[i]);
        if (ti.indirect) {
            if (read_block(ti.indirect, l1) == 0)
                for (uint32_t i = 0; i < PPB; i++)
                    if (l1[i]) free_block_num(l1[i]);
            free_block_num(ti.indirect);
        }
        if (ti.dindirect) {
            if (read_block(ti.dindirect, l1) == 0) {
                for (uint32_t i = 0; i < PPB; i++) {
                    if (!l1[i]) continue;
                    if (read_block(l1[i], l2) == 0)
                        for (uint32_t j = 0; j < PPB; j++)
                            if (l2[j]) free_block_num(l2[j]);
                    free_block_num(l1[i]);
                }
            }
            free_block_num(ti.dindirect);
        }
        ps_invalidate();   /* freed pointer blocks must not be served from cache */
        free_flush();
    }
    return 0;
}

static int ycfs_rename_impl(const char* old_path, const char* new_path) {
    if (!initialized) return -1;
    uint32_t old_parent; char old_name[56];
    if (ycfs_resolve_parent(old_path, &old_parent, old_name) != 0) return -1;
    uint32_t new_parent; char new_name[56];
    if (ycfs_resolve_parent(new_path, &new_parent, new_name) != 0) return -1;
    if (!ycfs_check_access(old_parent, YCFS_WANT_W)) return -1;
    if (!ycfs_check_access(new_parent, YCFS_WANT_W)) return -1;

    uint32_t target_inode, target_type; uint64_t target_size;
    if (ycfs_lookup(old_parent, old_name, &target_inode, &target_type, &target_size) != 0) return -1;

    uint32_t existing_inode, existing_type; uint64_t existing_size;
    if (ycfs_lookup(new_parent, new_name, &existing_inode, &existing_type, &existing_size) == 0)
        return -1; /* destination already exists */

    /* A directory can't be moved into its own subtree (it would vanish
     * from the namespace, taking everything under it along). */
    if (target_type == YCFS_TYPE_DIR) {
        const char* a = old_path; const char* b = new_path;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && (*b == '/' || *b == 0)) return -1;
    }

    txn_begin();
    if (append_dirent(new_parent, target_inode, new_name, target_type) != 0) { txn_commit(); return -1; }
    int r = clear_dirent(old_parent, old_name);
    txn_commit();
    return r;
}

/* Returns 0 and fills uid_out/gid_out/perm_out on success. */
static int ycfs_get_owner_impl(const char* path, uint32_t* uid_out, uint32_t* gid_out, uint16_t* perm_out) {
    if (!initialized) return -1;
    uint32_t inode, type; uint64_t size;
    if (ycfs_resolve(path, &inode, &type, &size) != 0) return -1;
    ycfs_inode_t ti;
    if (read_inode(inode, &ti) != 0) return -1;
    if (uid_out)  *uid_out  = ti.uid;
    if (gid_out)  *gid_out  = ti.gid;
    if (perm_out) *perm_out = ti.perm;
    return 0;
}

/* chmod — only the file's owner, root, or an elevated (youdo) session
 * may change its permission bits. Matches real POSIX chmod semantics
 * (owner-or-root), extended with youdo as a root-equivalent. */
static int ycfs_chmod_impl(const char* path, uint16_t perm) {
    if (!initialized) return -1;
    uint32_t inode, type; uint64_t size;
    if (ycfs_resolve(path, &inode, &type, &size) != 0) return -1;
    ycfs_inode_t ti;
    if (read_inode(inode, &ti) != 0) return -1;

    uint32_t caller_uid = YCFS_ROOT_UID, caller_gid = YCFS_ROOT_GID;
    session_lookup(&caller_uid, &caller_gid); /* leaves root defaults on failure */
    int allowed = (caller_uid == YCFS_ROOT_UID) || session_is_elevated() || (caller_uid == ti.uid);
    if (!allowed) return -1;

    ti.perm = perm & 0777;
    txn_begin();
    int r = write_inode(inode, &ti);
    txn_commit();
    return r;
}

/* chown — root or an elevated (youdo) session only. Real POSIX chown
 * restricts this to root even for the file's own owner (prevents
 * giving away files to dodge disk-quota accounting); no reason to be
 * looser here. Sets both uid and gid together, matching this OS's
 * uid==gid private-group convention (see mkycfs.py / auth_create_user). */
static int ycfs_chown_impl(const char* path, uint32_t new_uid, uint32_t new_gid) {
    if (!initialized) return -1;
    uint32_t inode, type; uint64_t size;
    if (ycfs_resolve(path, &inode, &type, &size) != 0) return -1;

    uint32_t caller_uid = YCFS_ROOT_UID, caller_gid = YCFS_ROOT_GID;
    session_lookup(&caller_uid, &caller_gid);
    int allowed = (caller_uid == YCFS_ROOT_UID) || session_is_elevated();
    if (!allowed) return -1;

    ycfs_inode_t ti;
    if (read_inode(inode, &ti) != 0) return -1;
    ti.uid = new_uid;
    ti.gid = new_gid;
    txn_begin();
    int r = write_inode(inode, &ti);
    txn_commit();
    return r;
}

static int ycfs_stat_impl(const char* path, uint32_t* size_out, uint8_t* is_dir_out) {
    if (!initialized) return -1;
    uint32_t inode, type; uint64_t size;
    if (ycfs_resolve(path, &inode, &type, &size) != 0) return -1;
    if (size_out)   *size_out   = (uint32_t)size;
    if (is_dir_out) *is_dir_out = (type == YCFS_TYPE_DIR) ? 1 : 0;
    return 0;
}

static int ycfs_list_dir_impl(const char* path, ycfs_entry_t* entries, int max_entries) {
    if (!initialized) return -1;
    uint32_t dir_inode, dir_type; uint64_t dir_size_unused;
    if (ycfs_resolve(path, &dir_inode, &dir_type, &dir_size_unused) != 0) return -1;
    if (dir_type != YCFS_TYPE_DIR) return -1;
    if (!ycfs_check_access(dir_inode, YCFS_WANT_R)) return -1;

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
static int64_t ycfs_savefile_impl(const char* path, const void* buf, uint32_t size) {
    if (!initialized) return -1;
    uint32_t parent; char name[56];
    if (ycfs_resolve_parent(path, &parent, name) != 0) return -1;

    uint32_t inode, type; uint64_t existing_size;
    if (ycfs_lookup(parent, name, &inode, &type, &existing_size) != 0) {
        if (!ycfs_check_access(parent, YCFS_WANT_W)) return -1;
        if (ycfs_create(parent, name, YCFS_TYPE_FILE, &inode) != 0) return -1;
    } else {
        if (type != YCFS_TYPE_FILE) return -1;     /* never write file data into a directory */
        if (!ycfs_check_access(inode, YCFS_WANT_W)) return -1;
    }
    int64_t w = ycfs_write(inode, 0, size, buf);
    /* Overwriting with something shorter must shrink the file, otherwise
     * the old tail survives past the new end. */
    if (w >= 0 && existing_size > size) ycfs_truncate(inode, size);
    return w;
}

/* Write a piece of a file: offset 0 creates the file (or empties an
 * existing one, keeping its owner and permissions); later pieces append at
 * the given offset. This is what lets the file manager copy files of any
 * size in chunks instead of needing the whole file in memory. */
static int64_t ycfs_write_chunk_impl(const char* path, uint64_t offset, const void* buf, uint32_t len) {
    if (!initialized) return -1;
    uint32_t parent; char name[56];
    if (ycfs_resolve_parent(path, &parent, name) != 0) return -1;

    uint32_t inode, type; uint64_t existing_size;
    if (ycfs_lookup(parent, name, &inode, &type, &existing_size) != 0) {
        if (offset != 0) return -1;                 /* continuing a file that is not there */
        if (!ycfs_check_access(parent, YCFS_WANT_W)) return -1;
        if (ycfs_create(parent, name, YCFS_TYPE_FILE, &inode) != 0) return -1;
    } else {
        if (type != YCFS_TYPE_FILE) return -1;
        if (!ycfs_check_access(inode, YCFS_WANT_W)) return -1;
        if (offset == 0 && existing_size > 0 && ycfs_truncate(inode, 0) != 0) return -1;
    }
    return ycfs_write(inode, offset, len, buf);
}

/* Path-based read, mirroring ycfs_savefile's shape. Resolves the full
 * path (not just its parent, since there's no write/create fallback
 * needed here) and reads up to max_size bytes from offset 0. Returns
 * the number of bytes read, or -1 if the path doesn't resolve to a
 * file. */
static int64_t ycfs_read_file_impl(const char* path, void* buf, uint32_t max_size) {
    if (!initialized) return -1;
    uint32_t inode, type; uint64_t size;
    if (ycfs_resolve(path, &inode, &type, &size) != 0) return -1;
    if (type != YCFS_TYPE_FILE) return -1;
    return ycfs_read(inode, 0, max_size, buf);
}

/* =========================================================================
 * Storage lock: every public entry point serialises on the storage mutex, so two processes
 * can never be inside the filesystem at once -- even though either may sleep (waiting for the
 * disk) in the middle. The lock is recursive, so these functions can call each other.
 * ========================================================================= */
int64_t ycfs_read(uint32_t inode_num, uint64_t offset, uint32_t size, void* buf) {
    storage_lock();
    int64_t r = ycfs_read_impl(inode_num, offset, size, buf);
    storage_unlock();
    return r;
}
int ycfs_lookup(uint32_t dir_inode, const char* name,
                uint32_t* out_inode, uint32_t* out_type, uint64_t* out_size) {
    storage_lock();
    int r = ycfs_lookup_impl(dir_inode, name, out_inode, out_type, out_size);
    storage_unlock();
    return r;
}
int ycfs_journal_self_test(void) {
    storage_lock();
    int r = ycfs_journal_self_test_impl();
    storage_unlock();
    return r;
}
int64_t ycfs_write(uint32_t inode_num, uint64_t offset, uint32_t size, const void* buf) {
    storage_lock();
    int64_t r = ycfs_write_impl(inode_num, offset, size, buf);
    storage_unlock();
    return r;
}
int ycfs_create(uint32_t dir_inode, const char* name, uint32_t type, uint32_t* out_inode) {
    storage_lock();
    int r = ycfs_create_impl(dir_inode, name, type, out_inode);
    storage_unlock();
    return r;
}
int ycfs_check_access(uint32_t inode_num, int want) {
    storage_lock();
    int r = ycfs_check_access_impl(inode_num, want);
    storage_unlock();
    return r;
}
int ycfs_mkdir(const char* path) {
    storage_lock();
    int r = ycfs_mkdir_impl(path);
    storage_unlock();
    return r;
}
int ycfs_unlink(const char* path) {
    storage_lock();
    int r = ycfs_unlink_impl(path);
    storage_unlock();
    return r;
}
int ycfs_rename(const char* old_path, const char* new_path) {
    storage_lock();
    int r = ycfs_rename_impl(old_path, new_path);
    storage_unlock();
    return r;
}
int ycfs_get_owner(const char* path, uint32_t* uid_out, uint32_t* gid_out, uint16_t* perm_out) {
    storage_lock();
    int r = ycfs_get_owner_impl(path, uid_out, gid_out, perm_out);
    storage_unlock();
    return r;
}
int ycfs_chmod(const char* path, uint16_t perm) {
    storage_lock();
    int r = ycfs_chmod_impl(path, perm);
    storage_unlock();
    return r;
}
int ycfs_chown(const char* path, uint32_t new_uid, uint32_t new_gid) {
    storage_lock();
    int r = ycfs_chown_impl(path, new_uid, new_gid);
    storage_unlock();
    return r;
}
int ycfs_stat(const char* path, uint32_t* size_out, uint8_t* is_dir_out) {
    storage_lock();
    int r = ycfs_stat_impl(path, size_out, is_dir_out);
    storage_unlock();
    return r;
}
int ycfs_list_dir(const char* path, ycfs_entry_t* entries, int max_entries) {
    storage_lock();
    int r = ycfs_list_dir_impl(path, entries, max_entries);
    storage_unlock();
    return r;
}
int64_t ycfs_savefile(const char* path, const void* buf, uint32_t size) {
    storage_lock();
    int64_t r = ycfs_savefile_impl(path, buf, size);
    storage_unlock();
    return r;
}
int64_t ycfs_write_chunk(const char* path, uint64_t offset, const void* buf, uint32_t len) {
    storage_lock();
    int64_t r = ycfs_write_chunk_impl(path, offset, buf, len);
    storage_unlock();
    return r;
}
int64_t ycfs_read_file(const char* path, void* buf, uint32_t max_size) {
    storage_lock();
    int64_t r = ycfs_read_file_impl(path, buf, max_size);
    storage_unlock();
    return r;
}
