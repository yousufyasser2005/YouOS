/*
 * fat16_vfs.c — VFS adapter for the FAT16 driver
 *
 * Mounts the FAT16 disk volume as a directory node at /disk.
 * Files under /disk/ are backed by fat16_open/read/write/close.
 */
#include <kernel/vfs.h>
#include <kernel/fat16.h>
#include <kernel/heap.h>
#include <kernel/vga.h>

/* ── Per-file VFS node private data ─────────────────────────── */
typedef struct {
    int fat_fd;   /* fd returned by fat16_open */
} fat16_file_data_t;

/* ── VFS operations for a FAT16 file ────────────────────────── */
static uint64_t fat16_vfs_read(vfs_node_t* node, uint64_t offset,
                                uint64_t size, uint8_t* buf)
{
    fat16_file_data_t* d = (fat16_file_data_t*)node->fs_data;
    if (!d) return 0;
    /* fat16_read is sequential; we re-open to seek to offset */
    /* Simple approach: close and reopen, then skip bytes */
    /* (FAT16 driver always starts from position 0 on open) */
    /* For now: caller always reads from 0 via vfs_read offset tracking */
    (void)offset; /* offset handled by vfs_fd_t, we read sequentially */
    int n = fat16_read(d->fat_fd, buf, (uint32_t)size);
    return (n < 0) ? 0 : (uint64_t)n;
}

static uint64_t fat16_vfs_write(vfs_node_t* node, uint64_t offset,
                                 uint64_t size, const uint8_t* buf)
{
    fat16_file_data_t* d = (fat16_file_data_t*)node->fs_data;
    if (!d) return 0;
    (void)offset;
    int n = fat16_write(d->fat_fd, buf, (uint32_t)size);
    return (n < 0) ? 0 : (uint64_t)n;
}

static void fat16_vfs_close(vfs_node_t* node)
{
    fat16_file_data_t* d = (fat16_file_data_t*)node->fs_data;
    if (!d) return;
    fat16_close(d->fat_fd);
    kfree(d);
    node->fs_data = 0;
}

/* ── VFS operations for the /disk directory node ────────────── */
static vfs_node_t* fat16_vfs_finddir(vfs_node_t* dir, const char* name)
{
    (void)dir;

    /* Try to open the file on FAT16 */
    int fd = fat16_open(name);
    if (fd < 0) {
        /* Try creating (for O_CREAT paths) — don't auto-create here */
        return 0;
    }

    /* Allocate a vfs_node for this file */
    vfs_node_t* node = (vfs_node_t*)kzalloc(sizeof(vfs_node_t));
    if (!node) { fat16_close(fd); return 0; }

    /* Copy name */
    int i = 0;
    while (name[i] && i < VFS_NAME_MAX - 1) { node->name[i] = name[i]; i++; }
    node->name[i] = 0;

    node->type    = VFS_FILE;
    node->size    = 0; /* unknown without dirent read — ok for read */
    node->inode   = (uint64_t)fd;
    node->read    = fat16_vfs_read;
    node->write   = fat16_vfs_write;
    node->finddir = 0;
    node->close   = fat16_vfs_close;

    fat16_file_data_t* d = (fat16_file_data_t*)kzalloc(sizeof(fat16_file_data_t));
    if (!d) { kfree(node); fat16_close(fd); return 0; }
    d->fat_fd  = fd;
    node->fs_data = d;

    return node;
}

/* ── Mount point ─────────────────────────────────────────────── */
static vfs_node_t disk_node;

vfs_node_t* fat16_vfs_mount(void)
{
    for (int i = 0; i < VFS_NAME_MAX; i++) disk_node.name[i] = 0;
    disk_node.name[0] = 'd';
    disk_node.name[1] = 'i';
    disk_node.name[2] = 's';
    disk_node.name[3] = 'k';
    disk_node.type    = VFS_DIR;
    disk_node.size    = 0;
    disk_node.inode   = 0;
    disk_node.read    = 0;
    disk_node.write   = 0;
    disk_node.finddir = fat16_vfs_finddir;
    disk_node.close   = 0;
    disk_node.fs_data = 0;
    disk_node.next    = 0;
    return &disk_node;
}
