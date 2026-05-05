#include <stdint.h>
#include <kernel/vga.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>

#define MULTIBOOT2_MAGIC 0x36D76289

void kernel_main(uint32_t mb2_magic, uint32_t mb2_info) {
    (void)mb2_info;

    /* VGA */
    vga_init();
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN,  VGA_BLACK);
    vga_puts_color("                             Welcome to YouOS                                  \n", VGA_YELLOW,      VGA_BLACK);
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN,  VGA_BLACK);
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

    vga_puts("\n");
    vga_puts_color("  YouOS kernel is alive. Interrupts enabled.\n", VGA_WHITE, VGA_BLACK);
    vga_puts("\n");

    /* Test: trigger a breakpoint exception (INT3) — should be caught */
    vga_puts_color("  Testing IDT with breakpoint (INT3)...\n", VGA_DARK_GREY, VGA_BLACK);
    __asm__ volatile ("int3");
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Breakpoint caught and returned safely!\n");

    while (1) { __asm__ volatile ("hlt"); }
}
