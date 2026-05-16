#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_NAME_MAX    64
#define VFS_PATH_MAX    256
#define MAX_OPEN_FILES  64

/* File types */
#define VFS_FILE        1
#define VFS_DIR         2

/* Open flags */
#define O_RDONLY        0
#define O_WRONLY        1
#define O_RDWR          2
#define O_CREAT         4

/* VFS node — represents a file or directory */
typedef struct vfs_node vfs_node_t;

struct vfs_node {
    char        name[VFS_NAME_MAX];
    uint32_t    type;       /* VFS_FILE or VFS_DIR */
    uint64_t    size;
    uint64_t    inode;

    /* Operations */
    uint64_t (*read) (vfs_node_t* node, uint64_t offset, uint64_t size, uint8_t* buf);
    uint64_t (*write)(vfs_node_t* node, uint64_t offset, uint64_t size, const uint8_t* buf);
    vfs_node_t* (*finddir)(vfs_node_t* node, const char* name);

    /* Filesystem-private data */
    void*       fs_data;
    vfs_node_t* next;   /* sibling in directory */
};

/* Open file descriptor */
typedef struct {
    vfs_node_t* node;
    uint64_t    offset;
    int         used;
} vfs_fd_t;

/* VFS API */
void        vfs_init(void);
void        vfs_mount_root(vfs_node_t* root);

int         vfs_open(const char* path, int flags);
int         vfs_close(int fd);
uint64_t    vfs_read(int fd, void* buf, uint64_t size);
uint64_t    vfs_write(int fd, const void* buf, uint64_t size);
vfs_node_t* vfs_resolve(const char* path);

#endif
