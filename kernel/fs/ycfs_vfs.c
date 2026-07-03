/*
 * ycfs_vfs.c — VFS adapter for YCFS
 *
 * Mounts YCFS at /ycfs. Each vfs_node_t just remembers which YCFS inode
 * number it represents (via the existing vfs_node_t.inode field); read
 * and finddir both operate relative to that. Unlike fat16_vfs.c, no
 * internal fd/position tracking is needed here — vfs_fd_t already
 * tracks the read offset for us, and ycfs_read() takes an explicit
 * offset per call.
 */
#include <kernel/vfs.h>
#include <kernel/ycfs.h>
#include <kernel/heap.h>
#include <kernel/vga.h>

static uint64_t ycfs_vfs_read(vfs_node_t* node, uint64_t offset,
                               uint64_t size, uint8_t* buf) {
    int64_t n = ycfs_read((uint32_t)node->inode, offset, (uint32_t)size, buf);
    return (n < 0) ? 0 : (uint64_t)n;
}

static void ycfs_vfs_close(vfs_node_t* node) {
    (void)node; /* nothing to free — no per-open state at the ycfs.c layer */
}

static vfs_node_t* ycfs_vfs_finddir(vfs_node_t* dir, const char* name) {
    uint32_t child_inode, child_type;
    uint64_t child_size;
    if (ycfs_lookup((uint32_t)dir->inode, name, &child_inode, &child_type, &child_size) != 0)
        return 0;

    vfs_node_t* node = (vfs_node_t*)kzalloc(sizeof(vfs_node_t));
    if (!node) return 0;

    int i = 0;
    while (name[i] && i < VFS_NAME_MAX - 1) { node->name[i] = name[i]; i++; }
    node->name[i] = 0;

    node->type    = (child_type == YCFS_TYPE_DIR) ? VFS_DIR : VFS_FILE;
    node->size    = child_size;
    node->inode   = child_inode;
    node->read    = (child_type == YCFS_TYPE_DIR) ? 0 : ycfs_vfs_read;
    node->write   = 0; /* phase 2 */
    node->finddir = (child_type == YCFS_TYPE_DIR) ? ycfs_vfs_finddir : 0;
    node->close   = ycfs_vfs_close;
    node->fs_data = 0;
    node->next    = 0;
    node->dynamic = 1; /* heap-allocated — safe for vfs_resolve to free
                          if this ends up being an intermediate node */
    return node;
}

static vfs_node_t ycfs_root_node;

vfs_node_t* ycfs_vfs_mount(void) {
    if (ycfs_init() != 0) return 0;

    for (int i = 0; i < VFS_NAME_MAX; i++) ycfs_root_node.name[i] = 0;
    ycfs_root_node.name[0] = 'y';
    ycfs_root_node.name[1] = 'c';
    ycfs_root_node.name[2] = 'f';
    ycfs_root_node.name[3] = 's';
    ycfs_root_node.type    = VFS_DIR;
    ycfs_root_node.size    = 0;
    ycfs_root_node.inode   = ycfs_root_inode();
    ycfs_root_node.read    = 0;
    ycfs_root_node.write   = 0;
    ycfs_root_node.finddir = ycfs_vfs_finddir;
    ycfs_root_node.close   = 0;
    ycfs_root_node.fs_data = 0;
    ycfs_root_node.next    = 0;
    ycfs_root_node.dynamic = 0; /* static — vfs_resolve must never free this */
    return &ycfs_root_node;
}
