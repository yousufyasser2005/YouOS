#ifndef KERNEL_VMM_H
#define KERNEL_VMM_H

#include <stdint.h>
#include <stddef.h>

/*
 * Page size and alignment
 */
#define PAGE_SIZE       4096
#define PAGE_MASK       (~(PAGE_SIZE - 1))
#define PAGE_ALIGN(a)   (((a) + PAGE_SIZE - 1) & PAGE_MASK)

/*
 * Page Table Entry flags
 */
#define PTE_PRESENT     (1ULL << 0)   /* Page is present         */
#define PTE_WRITABLE    (1ULL << 1)   /* Page is writable        */
#define PTE_USER        (1ULL << 2)   /* User-mode accessible    */
#define PTE_HUGE        (1ULL << 7)   /* 2MB huge page           */
#define PTE_NO_EXEC     (1ULL << 63)  /* No execute (NX bit)     */

/*
 * Page Table Entry — address mask
 * Bits 12-51 hold the physical address
 */
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL

/*
 * Virtual address breakdown (4-level paging):
 *   Bits 39-47 → PML4 index
 *   Bits 30-38 → PDP  index
 *   Bits 21-29 → PD   index
 *   Bits 12-20 → PT   index
 *   Bits  0-11 → Page offset
 */
#define PML4_INDEX(va)  (((va) >> 39) & 0x1FF)
#define PDP_INDEX(va)   (((va) >> 30) & 0x1FF)
#define PD_INDEX(va)    (((va) >> 21) & 0x1FF)
#define PT_INDEX(va)    (((va) >> 12) & 0x1FF)

/*
 * Kernel virtual base — where the kernel lives in virtual memory
 */
#define KERNEL_VIRT_BASE    0xFFFFFFFF80000000ULL
#define KERNEL_HEAP_START   0xFFFF800000000000ULL
#define KERNEL_HEAP_END     0xFFFF800040000000ULL  /* 1GB kernel heap */

/*
 * Page table type
 */
typedef uint64_t pte_t;
typedef pte_t    page_table_t[512];

/*
 * Address space (one per process, kernel shares one)
 */
typedef struct {
    pte_t*   pml4;          /* Physical address of PML4 table */
    uint64_t pml4_phys;
} address_space_t;

/* Initialize VMM — create kernel address space */
void vmm_init(void);

/* Map a virtual address to a physical address in given address space */
int vmm_map(address_space_t* as, uint64_t virt, uint64_t phys, uint64_t flags);

/* Unmap a virtual address */
void vmm_unmap(address_space_t* as, uint64_t virt);

/* Get physical address mapped at a virtual address (0 if not mapped) */
uint64_t vmm_get_phys(address_space_t* as, uint64_t virt);

/* Switch to an address space (load its PML4 into CR3) */
void vmm_switch(address_space_t* as);

/* The kernel's own address space */
extern address_space_t kernel_as;

#endif /* KERNEL_VMM_H */
