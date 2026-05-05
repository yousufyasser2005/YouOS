#include <stdint.h>
#include <kernel/vga.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>

#define MULTIBOOT2_MAGIC 0x36D76289

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
    vga_puts("GDT loaded — kernel/user segments + TSS ready\n");

    idt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("IDT loaded — CPU exceptions handled\n");

    pmm_init((uint64_t)mb2_info);
    pmm_stats_t stats = pmm_get_stats();

    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Physical Memory Manager initialized\n");

    vga_puts_color("       Total RAM : ", VGA_DARK_GREY, VGA_BLACK);
    print_uint64(stats.total_bytes / (1024 * 1024));
    vga_puts(" MB\n");

    vga_puts_color("       Free pages: ", VGA_DARK_GREY, VGA_BLACK);
    print_uint64(stats.free_pages);
    vga_puts(" (");
    print_uint64(stats.free_pages * 4 / 1024);
    vga_puts(" MB free)\n");

    /* Test alloc/free */
    uint64_t p = pmm_alloc_page();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Test page allocated at 0x");
    char hex[17]; uint64_t v = p;
    for (int i=15;i>=0;i--){hex[i]="0123456789ABCDEF"[v&0xF];v>>=4;}
    hex[16]=0; int s=0; while(s<15&&hex[s]=='0')s++;
    vga_puts(&hex[s]); vga_puts("\n");
    pmm_free_page(p);
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Test page freed\n");

    vga_puts("\n");
    vga_puts_color("  YouOS kernel is alive. Memory management active.\n", VGA_WHITE, VGA_BLACK);

    while (1) { __asm__ volatile ("hlt"); }
}
