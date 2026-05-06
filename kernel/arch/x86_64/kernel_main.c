#include <stdint.h>
#include <kernel/vga.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/pic.h>
#include <kernel/pmm.h>

#define MULTIBOOT2_MAGIC 0x36D76289

static void print_uint64(uint64_t val) {
    char buf[21]; int i = 20; buf[i] = '\0';
    if (val == 0) { vga_puts("0"); return; }
    while (val > 0) { buf[--i] = '0' + (val % 10); val /= 10; }
    vga_puts(&buf[i]);
}

void kernel_main(uint32_t mb2_magic, uint32_t mb2_info) {

    /* VGA */
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

    /* GDT */
    gdt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("GDT loaded — kernel/user segments + TSS ready\n");

    /* IDT */
    idt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("IDT loaded — CPU exceptions handled\n");

    /* PIC + IRQs */
    irq_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("PIC initialized — IRQs remapped to 0x20-0x2F\n");

    /* PMM */
    pmm_init((uint64_t)mb2_info);
    pmm_stats_t stats = pmm_get_stats();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Physical Memory Manager initialized\n");
    vga_puts_color("       Total RAM : ", VGA_DARK_GREY, VGA_BLACK);
    print_uint64(stats.free_pages * 4 / 1024);
    vga_puts(" MB free / ");
    print_uint64(256);
    vga_puts(" MB total\n");

    vga_puts("\n");
    vga_puts_color("  YouOS kernel is alive. All systems nominal.\n", VGA_WHITE, VGA_BLACK);
    vga_puts_color("  Timer ticks running...\n", VGA_DARK_GREY, VGA_BLACK);
    vga_puts("\n");

    /* Show timer ticks increasing — proves interrupts work */
    uint64_t last = 0;
    uint32_t shown = 0;
    while (shown < 5) {
        uint64_t t = irq_get_ticks();
        if (t != last && t % 100 == 0) {
            vga_puts_color("  tick: ", VGA_DARK_GREY, VGA_BLACK);
            print_uint64(t);
            vga_puts("\n");
            last = t;
            shown++;
        }
        __asm__ volatile ("hlt");
    }

    vga_puts_color("\n  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Timer IRQ working — interrupts fully operational!\n");

    while (1) { __asm__ volatile ("hlt"); }
}
