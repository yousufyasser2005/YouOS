/*
 * YouOS - Virtual Memory Manager
 * vmm.c - Fixed version
 *
 * Key insight: our boot page tables use 2MB HUGE pages for the identity map.
 * When vmm_map tries to walk through them to create a PT level,
 * it must replace the huge page with a proper PT table first.
 */

#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>

address_space_t kernel_as;

/* Allocate and zero a page table */
static pte_t* alloc_table(void)
{
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    pte_t* t = (pte_t*)phys;
    for (int i = 0; i < 512; i++) t[i] = 0;
    return t;
}

/*
 * Get or create next level table.
 * If entry is a huge page, replace it with a proper PT.
 */
static pte_t* get_or_create(pte_t* table, uint64_t idx, uint64_t flags)
{
    pte_t entry = table[idx];

    /* Entry present and NOT a huge page — just return it */
    if ((entry & PTE_PRESENT) && !(entry & PTE_HUGE)) {
        return (pte_t*)(entry & PTE_ADDR_MASK);
    }

    /* Entry is a huge page — we need to split it into 4KB pages */
    if ((entry & PTE_PRESENT) && (entry & PTE_HUGE)) {
        pte_t* new_pt = alloc_table();
        if (!new_pt) return 0;

        /* Get the 2MB base address this huge page covers */
        uint64_t huge_base = entry & 0x000FFFFFFFE00000ULL;
        uint64_t pt_flags  = (entry & 0xFFF) & ~PTE_HUGE;

        /* Fill new PT with 4KB pages covering same physical range */
        for (int i = 0; i < 512; i++) {
            new_pt[i] = (huge_base + (uint64_t)i * PAGE_SIZE)
                       | pt_flags | PTE_PRESENT;
        }

        /* Replace huge page entry with new PT */
        table[idx] = (uint64_t)new_pt | flags | PTE_PRESENT;
        return new_pt;
    }

    /* Entry not present — allocate new table */
    pte_t* new_table = alloc_table();
    if (!new_table) return 0;
    table[idx] = (uint64_t)new_table | flags | PTE_PRESENT;
    return new_table;
}

int vmm_map(address_space_t* as, uint64_t virt, uint64_t phys, uint64_t flags)
{
    pte_t* pml4 = as->pml4;

    pte_t* pdp = get_or_create(pml4, PML4_INDEX(virt),
                                PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    if (!pdp) return -1;

    pte_t* pd  = get_or_create(pdp,  PDP_INDEX(virt),
                                PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    if (!pd) return -1;

    pte_t* pt  = get_or_create(pd,   PD_INDEX(virt),
                                PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    if (!pt) return -1;

    pt[PT_INDEX(virt)] = (phys & PTE_ADDR_MASK) | flags | PTE_PRESENT;

    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

void vmm_unmap(address_space_t* as, uint64_t virt)
{
    pte_t* pml4 = as->pml4;
    if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) return;

    pte_t* pdp = (pte_t*)(pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);
    if (!(pdp[PDP_INDEX(virt)] & PTE_PRESENT)) return;

    pte_t* pd  = (pte_t*)(pdp[PDP_INDEX(virt)] & PTE_ADDR_MASK);
    if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) return;
    if (pd[PD_INDEX(virt)] & PTE_HUGE) return;  /* don't unmap huge pages */

    pte_t* pt  = (pte_t*)(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);
    pt[PT_INDEX(virt)] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t vmm_get_phys(address_space_t* as, uint64_t virt)
{
    pte_t* pml4 = as->pml4;
    if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) return 0;

    pte_t* pdp = (pte_t*)(pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);
    if (!(pdp[PDP_INDEX(virt)] & PTE_PRESENT)) return 0;

    pte_t* pd  = (pte_t*)(pdp[PDP_INDEX(virt)] & PTE_ADDR_MASK);
    if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) return 0;
    if (pd[PD_INDEX(virt)] & PTE_HUGE)
        return (pd[PD_INDEX(virt)] & 0x000FFFFFFFE00000ULL) + (virt & 0x1FFFFF);

    pte_t* pt  = (pte_t*)(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);
    if (!(pt[PT_INDEX(virt)] & PTE_PRESENT)) return 0;
    return (pt[PT_INDEX(virt)] & PTE_ADDR_MASK) + (virt & 0xFFF);
}

void vmm_switch(address_space_t* as)
{
    __asm__ volatile (
        "mov %0, %%cr3"
        : : "r"((uint64_t)as->pml4) : "memory"
    );
}

void vmm_init(void)
{
    /* Allocate PML4 */
    kernel_as.pml4      = alloc_table();
    kernel_as.pml4_phys = (uint64_t)kernel_as.pml4;

    if (!kernel_as.pml4) {
        vga_puts_color("  [!!] VMM: PML4 alloc failed!\n",
                       VGA_LIGHT_RED, VGA_BLACK);
        return;
    }

    /*
     * Identity map first 1GB using 2MB huge pages.
     * This keeps the kernel running after CR3 switch.
     * We map it as a PD directly under PDP[0] → PML4[0].
     */
    pte_t* pdp = alloc_table();
    if (!pdp) return;
    kernel_as.pml4[0] = (uint64_t)pdp | PTE_PRESENT | PTE_WRITABLE;

    pte_t* pd = alloc_table();
    if (!pd) return;
    pdp[0] = (uint64_t)pd | PTE_PRESENT | PTE_WRITABLE;

    /* 512 × 2MB huge pages = 1GB identity map */
    for (int i = 0; i < 512; i++) {
        pd[i] = ((uint64_t)i << 21) | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE;
    }

    /* Switch to new page tables */
    vmm_switch(&kernel_as);
}
