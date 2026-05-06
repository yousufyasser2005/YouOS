#include <stdint.h>
#include <kernel/vga.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/pic.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/heap.h>
#include <kernel/terminal.h>

#define MULTIBOOT2_MAGIC 0x36D76289

/* Simple strcmp */
static int kstrcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

void kernel_main(uint32_t mb2_magic, uint32_t mb2_info) {

    /* Boot sequence */
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
    char buf[21]; int i = 20; buf[i] = '\0';
    uint64_t mb = stats.free_pages * 4 / 1024;
    if (mb == 0) { buf[--i] = '0'; }
    else while (mb > 0) { buf[--i] = '0' + (mb % 10); mb /= 10; }
    vga_puts(&buf[i]);
    vga_puts(" MB free\n");

    vmm_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("VMM initialized\n");

    heap_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Heap initialized\n");

    terminal_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Keyboard driver loaded\n");

    vga_puts("\n");
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts_color("  YouOS shell — type 'help' for commands\n", VGA_WHITE, VGA_BLACK);
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");

    /* Simple command shell */
    char line[256];
    while (1) {
        vga_puts_color("YouOS> ", VGA_LIGHT_GREEN, VGA_BLACK);
        terminal_readline(line, sizeof(line));

        if (kstrcmp(line, "help") == 0) {
            vga_puts_color("  Commands:\n", VGA_LIGHT_CYAN, VGA_BLACK);
            vga_puts("    help     - show this message\n");
            vga_puts("    clear    - clear the screen\n");
            vga_puts("    mem      - show memory stats\n");
            vga_puts("    heap     - show heap stats\n");
            vga_puts("    version  - show YouOS version\n");
            vga_puts("    reboot   - reboot the system\n");

        } else if (kstrcmp(line, "clear") == 0) {
            vga_clear();

        } else if (kstrcmp(line, "mem") == 0) {
            pmm_stats_t s = pmm_get_stats();
            vga_puts_color("  Memory:\n", VGA_LIGHT_CYAN, VGA_BLACK);
            vga_puts("    Total pages : "); 
            char b[21]; int j=20; b[j]='\0';
            uint64_t v=s.total_pages; if(!v){b[--j]='0';}
            else while(v){b[--j]='0'+(v%10);v/=10;}
            vga_puts(&b[j]); vga_puts("\n");
            vga_puts("    Free  pages : ");
            j=20; b[j]='\0'; v=s.free_pages; if(!v){b[--j]='0';}
            else while(v){b[--j]='0'+(v%10);v/=10;}
            vga_puts(&b[j]); vga_puts("\n");
            vga_puts("    Used  pages : ");
            j=20; b[j]='\0'; v=s.used_pages; if(!v){b[--j]='0';}
            else while(v){b[--j]='0'+(v%10);v/=10;}
            vga_puts(&b[j]); vga_puts("\n");

        } else if (kstrcmp(line, "heap") == 0) {
            heap_dump_stats();

        } else if (kstrcmp(line, "version") == 0) {
            vga_puts_color("  YouOS v0.1.0\n", VGA_YELLOW, VGA_BLACK);
            vga_puts("  Architecture : x86_64\n");
            vga_puts("  Built from scratch — no Linux\n");

        } else if (kstrcmp(line, "reboot") == 0) {
            vga_puts_color("  Rebooting...\n", VGA_YELLOW, VGA_BLACK);
            /* Reboot via keyboard controller reset line */
            __asm__ volatile (
                "cli\n"
                "outb %%al, $0x64\n"
                : : "a"((uint8_t)0xFE)
            );
            /* If that fails, try triple fault */
            __asm__ volatile ("lidt 0(%%rax)\n int $0\n" : : "a"(0));
            while(1) __asm__ volatile ("hlt");

        } else if (line[0] != '\0') {
            vga_puts_color("  Unknown command: ", VGA_LIGHT_RED, VGA_BLACK);
            vga_puts(line);
            vga_puts("\n  Type 'help' for available commands.\n");
        }
    }
}
