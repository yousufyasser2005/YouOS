#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

#include <stdint.h>
#include <stddef.h>

/*
 * Kernel heap region
 * Lives at a fixed virtual address, backed by physical pages on demand
 */
#define HEAP_START      0xFFFF800000000000ULL
#define HEAP_END        0xFFFF800040000000ULL   /* 1GB max */
#define HEAP_INIT_SIZE  (4 * 1024 * 1024)       /* Start with 4MB */

/*
 * Block header — stored just before every allocation
 *
 * Memory layout:
 *   [ block_header_t | <user data> | ... ]
 *   ^                ^
 *   header           pointer returned to caller
 */
typedef struct block_header {
    uint32_t            magic;      /* 0xDEADC0DE = valid block    */
    uint32_t            size;       /* Size of user data (bytes)   */
    uint8_t             free;       /* 1 = free, 0 = allocated     */
    uint8_t             _pad[3];
    struct block_header* next;      /* Next block in free list     */
    struct block_header* prev;      /* Previous block              */
} __attribute__((packed)) block_header_t;

#define HEAP_MAGIC      0xDEADC0DE
#define HEADER_SIZE     sizeof(block_header_t)

/* Initialize the kernel heap */
void heap_init(void);

/* Allocate size bytes — returns pointer or NULL */
void* kmalloc(size_t size);

/* Allocate and zero size bytes */
void* kzalloc(size_t size);

/* Allocate size bytes aligned to align boundary */
void* kmalloc_aligned(size_t size, size_t align);

/* Free a pointer returned by kmalloc */
void kfree(void* ptr);

/* Print heap statistics to VGA */
void heap_dump_stats(void);

#endif /* KERNEL_HEAP_H */
