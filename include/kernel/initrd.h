#ifndef KERNEL_INITRD_H
#define KERNEL_INITRD_H

#include <stdint.h>

/* Simple initrd format:
   [header: magic(4) + count(4)]
   [file entries: name(32) + offset(8) + size(8)] * count
   [file data...]
*/

#define INITRD_MAGIC 0x59524449  /* "IDRY" */
#define INITRD_NAME_MAX 32

typedef struct {
    char     name[INITRD_NAME_MAX];
    uint64_t offset;   /* offset from start of initrd */
    uint64_t size;
} __attribute__((packed)) initrd_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t count;
} __attribute__((packed)) initrd_header_t;

/* Initialize initrd from embedded data */
void initrd_init(void);

/* Find a file — returns pointer to data and sets size, or NULL */
const void* initrd_find(const char* name, uint64_t* size);

#endif
