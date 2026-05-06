#include <stdint.h>
#include <kernel/vga.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/pic.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>

#define MULTIBOOT2_MAGIC 0x36D76289

static void print_hex(uint64_t val) {
    char hex[17];
    for (int i = 15; i >= 0; i--) {
        hex[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
    }
    hex[16] = 0;
    int s = 0;
    while (s < 15 && hex[s] == '0') s++;
    vga_puts("0x");
    vga_puts(&hex[s]);
}

static void print_uint64(uint64_t val) {
    char buf[21]; int i = 20; buf[i] = '\0';
    if (val == 0) { vga_puts("0"); return; }
    while (val > 0) { buf[--i] = '0' + (val % 10); val /= 10; }
    vga_puts(&buf[i]);
}

void kernel_main(uint32_t mb2_magic, uint32_t mb2_info) {

    vga_init();
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts_color("                             Welcome to YouOS                                  \n", VGA_YELLOW,     VGA_BLACK);
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");

    if (mb2_magic == MULTIBOOT2_MAGIC) {
        vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("Multiboot2 bootloader detected\n");
    } else {
        vga_puts_color("  [!!] ", VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("WARNING: Invalid Multiboot2 magic!\n");
    }
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("VGA driver initialized\n");

    gdt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("GDT loaded\n");

    idt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("IDT loaded\n");

    irq_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("PIC initialized\n");

    pmm_init((uint64_t)mb2_info);
    pmm_stats_t stats = pmm_get_stats();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("PMM initialized — ");
    print_uint64(stats.free_pages * 4 / 1024);
    vga_puts(" MB free\n");

    vmm_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("VMM initialized\n");

    /* Debug: what physical address does PMM give us? */
    uint64_t p1 = pmm_alloc_page();
    uint64_t p2 = pmm_alloc_page();
    uint64_t p3 = pmm_alloc_page();
    vga_puts_color("  [..] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("PMM alloc test pages: ");
    print_hex(p1); vga_puts(" ");
    print_hex(p2); vga_puts(" ");
    print_hex(p3); vga_puts("\n");
    pmm_free_page(p1);
    pmm_free_page(p2);
    pmm_free_page(p3);

    /* Debug: what is current CR3? */
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    vga_puts_color("  [..] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("CR3 = "); print_hex(cr3); vga_puts("\n");

    /* Debug: PML4 address */
    vga_puts_color("  [..] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("PML4 = "); print_hex((uint64_t)kernel_as.pml4); vga_puts("\n");

    /* Try mapping a page WITHIN identity mapped region */
    uint64_t test_phys = pmm_alloc_page();
    vga_puts_color("  [..] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("test_phys = "); print_hex(test_phys); vga_puts("\n");

    /* Use a virtual address we KNOW is in identity map (below 1GB = 0x40000000) */
    uint64_t test_virt = 0x20000000;
    vga_puts_color("  [..] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("mapping "); print_hex(test_virt);
    vga_puts(" -> "); print_hex(test_phys); vga_puts("...\n");

    int result = vmm_map(&kernel_as, test_virt, test_phys,
                         PTE_PRESENT | PTE_WRITABLE);
    vga_puts_color("  [..] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("vmm_map returned: ");
    print_uint64((uint64_t)(result == 0 ? 0 : 1));
    vga_puts(result == 0 ? " (OK)\n" : " (FAIL)\n");

    /* Write via identity map */
    *((uint64_t*)test_phys) = 0xCAFEBABEULL;

    /* Read via mapped virt */
    uint64_t read_val = *((uint64_t*)test_virt);
    vga_puts_color("  [..] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("wrote 0xCAFEBABE via phys, read via virt: ");
    print_hex(read_val); vga_puts("\n");

    if (read_val == 0xCAFEBABEULL) {
        vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("VMM mapping WORKS!\n");
    } else {
        vga_puts_color("  [!!] ", VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("VMM mapping FAILED\n");
    }

    vmm_unmap(&kernel_as, test_virt);
    pmm_free_page(test_phys);

    vga_puts("\n");
    vga_puts_color("  YouOS kernel is alive. Virtual memory active.\n",
                   VGA_WHITE, VGA_BLACK);

    while (1) { __asm__ volatile ("hlt"); }
}
