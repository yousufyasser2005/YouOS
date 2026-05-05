#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdint.h>
#include <stddef.h>

/*
 * Page size — 4KB
 */
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

/*
 * Convert between addresses and page frame numbers
 */
#define ADDR_TO_PFN(addr)   ((addr) >> PAGE_SHIFT)
#define PFN_TO_ADDR(pfn)    ((pfn)  << PAGE_SHIFT)

/*
 * Multiboot2 structures (we only need the memory map tag)
 */
#define MB2_TAG_END         0
#define MB2_TAG_MEMMAP      6

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed)) mb2_tag_memmap_t;

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;          /* 1 = available RAM */
    uint32_t reserved;
} __attribute__((packed)) mb2_memmap_entry_t;

/*
 * Memory region types
 */
#define MB2_MEM_AVAILABLE   1
#define MB2_MEM_RESERVED    2
#define MB2_MEM_ACPI        3
#define MB2_MEM_NVS         4
#define MB2_MEM_BADRAM      5

/*
 * PMM statistics
 */
typedef struct {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t total_bytes;
} pmm_stats_t;

/* Initialize the PMM from multiboot2 memory map */
void pmm_init(uint64_t mb2_info_addr);

/* Allocate one physical page — returns physical address or 0 on failure */
uint64_t pmm_alloc_page(void);

/* Free a physical page */
void pmm_free_page(uint64_t addr);

/* Allocate n contiguous physical pages */
uint64_t pmm_alloc_pages(size_t n);

/* Get memory statistics */
pmm_stats_t pmm_get_stats(void);

#endif /* KERNEL_PMM_H */
