/*
 * ramfs — simple read-only RAM filesystem backed by initrd.
 * On top of this we'll later add a writable layer.
 */
#include <kernel/vfs.h>
#include <kernel/initrd.h>
#include <kernel/heap.h>
#include <kernel/vga.h>

/* Per-file private data */
typedef struct {
    const uint8_t* data;
    uint64_t       size;
} ramfs_file_t;

static uint64_t ramfs_read(vfs_node_t* node, uint64_t offset,
                            uint64_t size, uint8_t* buf) {
    ramfs_file_t* f = (ramfs_file_t*)node->fs_data;
    if (offset >= f->size) return 0;
    if (offset + size > f->size) size = f->size - offset;
    for (uint64_t i = 0; i < size; i++) buf[i] = f->data[offset + i];
    return size;
}

static vfs_node_t* ramfs_finddir(vfs_node_t* node, const char* name) {
    vfs_node_t* child = (vfs_node_t*)node->fs_data;
    while (child) {
        /* strcmp */
        const char* a = child->name;
        const char* b = name;
        int match = 1;
        while (*a && *b) { if (*a++ != *b++) { match = 0; break; } }
        if (match && !*a && !*b) return child;
        child = child->next;
    }
    return 0;
}

/* Build a ramfs tree from the initrd */
vfs_node_t* ramfs_init(void) {
    /* Root directory node */
    vfs_node_t* root = (vfs_node_t*)kzalloc(sizeof(vfs_node_t));
    root->name[0] = '/'; root->name[1] = 0;
    root->type    = VFS_DIR;
    root->finddir = ramfs_finddir;
    root->fs_data = 0;  /* will point to first child */

    /* Walk initrd and create file nodes */
    extern uint8_t _initrd_start[];
    const uint8_t* data = _initrd_start;

    typedef struct { uint32_t magic; uint32_t count; } hdr_t;
    typedef struct { char name[32]; uint64_t offset; uint64_t size; } entry_t;

    const hdr_t*   hdr     = (const hdr_t*)data;
    const entry_t* entries = (const entry_t*)(data + 8);

    vfs_node_t* last = 0;
    for (uint32_t i = 0; i < hdr->count; i++) {
        vfs_node_t* file = (vfs_node_t*)kzalloc(sizeof(vfs_node_t));
        ramfs_file_t* fd = (ramfs_file_t*)kzalloc(sizeof(ramfs_file_t));

        /* Copy name */
        for (int j = 0; j < 31 && entries[i].name[j]; j++)
            file->name[j] = entries[i].name[j];

        fd->data    = data + entries[i].offset;
        fd->size    = entries[i].size;
        file->type  = VFS_FILE;
        file->size  = entries[i].size;
        file->inode = i + 1;
        file->read  = ramfs_read;
        file->fs_data = fd;

        /* Link into root's child list */
        if (!last) root->fs_data = file;
        else       last->next    = file;
        last = file;
    }

    vga_puts_color("  [OK] ramfs mounted\n", VGA_LIGHT_GREEN, VGA_BLACK);
    return root;
}
