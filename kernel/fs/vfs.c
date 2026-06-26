#include <kernel/vfs.h>
#include <kernel/vga.h>
#include <kernel/heap.h>

static vfs_node_t* vfs_root = 0;
static vfs_fd_t    fd_table[MAX_OPEN_FILES];

void vfs_init(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) fd_table[i].used = 0;
    vga_puts_color("  [OK] VFS initialized\n", VGA_LIGHT_GREEN, VGA_BLACK);
}

void vfs_mount_root(vfs_node_t* root) {
    vfs_root = root;
    vga_puts_color("  [OK] Root filesystem mounted\n", VGA_LIGHT_GREEN, VGA_BLACK);
}

/* Resolve a path to a vfs_node */
vfs_node_t* vfs_resolve(const char* path) {
    if (!vfs_root) return 0;
    if (path[0] == '/') path++;  /* skip leading slash */

    /* Empty path = root */
    if (path[0] == 0) return vfs_root;

    /* Walk directories */
    vfs_node_t* node = vfs_root;
    char part[VFS_NAME_MAX];

    while (*path) {
        /* Extract next path component */
        int i = 0;
        while (*path && *path != '/') part[i++] = *path++;
        part[i] = 0;
        if (*path == '/') path++;

        /* Find child */
        if (!node->finddir) return 0;
        node = node->finddir(node, part);
        if (!node) return 0;
    }
    return node;
}

int vfs_open(const char* path, int flags) {
    (void)flags;
    vfs_node_t* node = vfs_resolve(path);
    if (!node) return -1;

    /* Find free fd slot */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!fd_table[i].used) {
            fd_table[i].node   = node;
            fd_table[i].offset = 0;
            fd_table[i].used   = 1;
            return i;
        }
    }
    return -1;  /* too many open files */
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table[fd].used) return -1;
    vfs_node_t* node = fd_table[fd].node;
    fd_table[fd].used = 0;
    if (node && node->close) {
        node->close(node);
        kfree(node);
    }
    return 0;
}

uint64_t vfs_read(int fd, void* buf, uint64_t size) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table[fd].used) return 0;
    vfs_fd_t*   f    = &fd_table[fd];
    vfs_node_t* node = f->node;
    if (!node->read) return 0;
    uint64_t n = node->read(node, f->offset, size, (uint8_t*)buf);
    f->offset += n;
    return n;
}

uint64_t vfs_write(int fd, const void* buf, uint64_t size) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || !fd_table[fd].used) return 0;
    vfs_fd_t*   f    = &fd_table[fd];
    vfs_node_t* node = f->node;
    if (!node->write) return 0;
    uint64_t n = node->write(node, f->offset, size, (const uint8_t*)buf);
    f->offset += n;
    return n;
}
