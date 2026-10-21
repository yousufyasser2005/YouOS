#ifndef KERNEL_YCFS_H
#define KERNEL_YCFS_H

#include <stdint.h>

/*
 * YCFS — Yousuf-Claude File System
 * Phase 1: read-only, real nested directories, bitmap-based free space.
 *
 * Starts 64MB into the disk (well past FAT16's own extent) and runs to
 * the end of the disk -- its size comes from the superblock, so the same
 * driver handles anything from a 16MB test image to a multi-TB drive.
 * No second physical drive needed, since the ATA driver only supports
 * one. Mounted at /ycfs alongside FAT16's /disk.
 *
 * Format versions:
 *   v1  one-block bitmaps (max 128MB), files up to 12 direct + 1 indirect
 *       block (~4MB).
 *   v2  multi-block bitmaps (any size up to 16TB), persistent allocation
 *       hints, plus a double-indirect block per inode (files up to ~4GB).
 * The driver reads both; v1 superblocks simply have zeros in the new
 * fields, which mean "one bitmap block, hint 0".
 */

#define YCFS_MAGIC          0x59434653u   /* "YCFS" */
#define YCFS_BLOCK_SIZE     4096
#define YCFS_START_LBA      131072u       /* 64MB / 512 */

#define YCFS_TYPE_FILE      1
#define YCFS_TYPE_DIR       2

#define YCFS_DIRECT_BLOCKS  12
#define YCFS_NAME_MAX       55             /* +1 for NUL = 56 */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t total_inodes;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t inode_bitmap_block;
    uint32_t block_bitmap_block;
    uint32_t inode_table_block;
    uint32_t inode_table_blocks;
    uint32_t data_start_block;
    uint32_t root_inode;
    uint32_t journal_start_block;  /* phase 3 */
    uint32_t journal_num_blocks;   /* 0 = no journal (old/unformatted) */
    /* ---- v2 fields (all zero in a v1 image) ---- */
    uint32_t block_bitmap_blocks;  /* length of the block bitmap in blocks; 0 = 1 */
    uint32_t inode_bitmap_blocks;  /* length of the inode bitmap in blocks; 0 = 1 */
    uint32_t block_hint;           /* where the next free-block search starts */
    uint32_t inode_hint;           /* where the next free-inode search starts */
    uint8_t  reserved[4096 - 76];
} __attribute__((packed)) ycfs_superblock_t;

/* ---- Phase 3: journaling ----
 * Write-ahead log: before any metadata write inside a transaction, log
 * {target_block, new content} as a (descriptor, data) block pair. Once
 * every write in the transaction is logged, write one commit record.
 * At mount, replay scans from journal block 0: any transaction whose
 * commit is present gets its logged writes re-applied (idempotent —
 * harmless even if the real write already landed before a crash);
 * anything logged without a matching commit (crash happened mid-
 * transaction) is discarded entirely, so the filesystem never ends up
 * half-updated. The journal is wiped after every replay so each boot
 * starts clean. */
#define YCFS_JOURNAL_MAGIC   0x594A524Eu  /* "YJRN" */
#define YCFS_JTYPE_DESC      1
#define YCFS_JTYPE_COMMIT    2

typedef struct {
    uint32_t magic;
    uint32_t txn_id;
    uint32_t target_block;   /* unused for YCFS_JTYPE_COMMIT */
    uint32_t type;
    uint8_t  reserved[4096 - 16];
} __attribute__((packed)) ycfs_journal_hdr_t;

int ycfs_journal_self_test(void);

/* Owner/group/permission bits, added for Phase 3.5 item 4 (multi-user
 * + POSIX-style permissions). uid 0 = root; every user gets a private
 * group by default (uid == gid), matching common Unix convention.
 * perm is a standard 9-bit rwxrwxrwx field (owner/group/other), same
 * bit layout as a traditional Unix mode (e.g. 0644, 0755). */
#define YCFS_PERM_R(shift) (0x4 << (shift))
#define YCFS_PERM_W(shift) (0x2 << (shift))
#define YCFS_PERM_X(shift) (0x1 << (shift))
#define YCFS_PERM_OWNER_SHIFT 6
#define YCFS_PERM_GROUP_SHIFT 3
#define YCFS_PERM_OTHER_SHIFT 0
#define YCFS_ROOT_UID 0
#define YCFS_ROOT_GID 0

typedef struct {
    uint32_t mode;          /* YCFS_TYPE_FILE or YCFS_TYPE_DIR */
    uint64_t size;
    uint32_t links_count;
    uint32_t blocks_used;
    uint32_t direct[YCFS_DIRECT_BLOCKS];
    uint32_t indirect;
    uint64_t mtime;
    uint32_t uid;            /* owner user ID (0 = root) */
    uint32_t gid;            /* owner group ID (0 = root group) */
    uint16_t perm;           /* rwxrwxrwx permission bits */
    uint32_t dindirect;      /* v2: block of pointers to indirect blocks (0 = none) */
    uint8_t  reserved[128 - (4+8+4+4+48+4+8+4+4+2+4)];
} __attribute__((packed)) ycfs_inode_t;

typedef struct {
    uint32_t inode;          /* 0 = unused slot */
    uint16_t rec_len;        /* reserved for future variable-length use */
    uint8_t  name_len;
    uint8_t  file_type;      /* YCFS_TYPE_FILE or YCFS_TYPE_DIR */
    char     name[56];
} __attribute__((packed)) ycfs_dirent_t;

int      ycfs_init(void);
uint32_t ycfs_root_inode(void);
int      ycfs_lookup(uint32_t dir_inode, const char* name,
                      uint32_t* out_inode, uint32_t* out_type, uint64_t* out_size);
int64_t  ycfs_read(uint32_t inode_num, uint64_t offset, uint32_t size, void* buf);

/* Phase 2: writes */
int64_t  ycfs_write(uint32_t inode_num, uint64_t offset, uint32_t size, const void* buf);
int      ycfs_create(uint32_t dir_inode, const char* name, uint32_t type, uint32_t* out_inode);
int      ycfs_check_access(uint32_t inode_num, int want);
int      ycfs_get_owner(const char* path, uint32_t* uid_out, uint32_t* gid_out, uint16_t* perm_out);
int      ycfs_chmod(const char* path, uint16_t perm);
int      ycfs_chown(const char* path, uint32_t new_uid, uint32_t new_gid);
#define YCFS_WANT_R 0x4
#define YCFS_WANT_W 0x2
#define YCFS_WANT_X 0x1

/* Same field layout as fat16_entry_t (char name[32]; uint32_t size;
 * uint8_t is_dir;) so listings look identical to callers regardless of
 * which filesystem backs a path. */
typedef struct {
    char     name[32];
    uint32_t size;
    uint8_t  is_dir;
} ycfs_entry_t;

/* Path-based ops, mirroring fat16's own convention: accept a full path
 * (leading "/ycfs/" or bare "ycfs/" prefix optional, stripped internally)
 * and resolve real nested directories via repeated ycfs_lookup(). */
int ycfs_unlink(const char* path);
int ycfs_mkdir(const char* path);
int ycfs_rename(const char* old_path, const char* new_path);
int ycfs_stat(const char* path, uint32_t* size_out, uint8_t* is_dir_out);
int ycfs_list_dir(const char* path, ycfs_entry_t* entries, int max_entries);
int64_t ycfs_savefile(const char* path, const void* buf, uint32_t size);
/* Piecewise write: offset 0 creates/empties the file, later offsets append. */
int64_t ycfs_write_chunk(const char* path, uint64_t offset, const void* buf, uint32_t len);
int64_t ycfs_read_file(const char* path, void* buf, uint32_t max_size);

#endif
