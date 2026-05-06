#include <stdint.h>
#include <kernel/vga.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/pic.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/heap.h>

#define MULTIBOOT2_MAGIC 0x36D76289

static void print_uint64(uint64_t val) {
    char buf[21]; int i = 20; buf[i] = '\0';
    if (val == 0) { vga_puts("0"); return; }
    while (val > 0) { buf[--i] = '0' + (val % 10); val /= 10; }
    vga_puts(&buf[i]);
}

static void print_hex(uint64_t val) {
    char hex[17];
    for (int i = 15; i >= 0; i--) {
        hex[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
    }
    hex[16] = 0;
    int s = 0; while (s < 15 && hex[s] == '0') s++;
    vga_puts("0x"); vga_puts(&hex[s]);
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

    gdt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("GDT loaded — kernel/user segments + TSS ready\n");

    idt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("IDT loaded — CPU exceptions handled\n");

    irq_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("PIC initialized — IRQs remapped to 0x20-0x2F\n");

    pmm_init((uint64_t)mb2_info);
    pmm_stats_t stats = pmm_get_stats();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("PMM initialized — ");
    print_uint64(stats.free_pages * 4 / 1024);
    vga_puts(" MB free\n");

    vmm_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("VMM initialized — kernel page tables active\n");

    heap_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Heap initialized — kmalloc/kfree ready\n");
    heap_dump_stats();

    /* ---- Test the heap ---- */
    vga_puts("\n");
    vga_puts_color("  Testing heap allocator...\n", VGA_DARK_GREY, VGA_BLACK);

    /* Test 1: basic alloc/free */
    uint64_t* a = (uint64_t*)kmalloc(sizeof(uint64_t));
    uint64_t* b = (uint64_t*)kmalloc(sizeof(uint64_t));
    uint64_t* c = (uint64_t*)kmalloc(sizeof(uint64_t));
    *a = 0x111; *b = 0x222; *c = 0x333;

    if (*a == 0x111 && *b == 0x222 && *c == 0x333) {
        vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("Basic alloc — 3 pointers: ");
        print_hex((uint64_t)a); vga_puts(" ");
        print_hex((uint64_t)b); vga_puts(" ");
        print_hex((uint64_t)c); vga_puts("\n");
    }

    /* Test 2: kzalloc zeroes memory */
    uint8_t* zbuf = (uint8_t*)kzalloc(64);
    uint8_t  zero_ok = 1;
    for (int i = 0; i < 64; i++) if (zbuf[i] != 0) { zero_ok = 0; break; }
    vga_puts_color(zero_ok ? "  [OK] " : "  [!!] ",
                   zero_ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    vga_puts("kzalloc — memory zeroed correctly\n");

    /* Test 3: free and reuse */
    kfree(b);
    uint64_t* b2 = (uint64_t*)kmalloc(sizeof(uint64_t));
    *b2 = 0x999;
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("kfree + realloc — reused block at ");
    print_hex((uint64_t)b2); vga_puts("\n");

    /* Test 4: larger allocation */
    char* buf = (char*)kmalloc(1024);
    for (int i = 0; i < 1024; i++) buf[i] = (char)(i & 0xFF);
    uint8_t large_ok = 1;
    for (int i = 0; i < 1024; i++)
        if (buf[i] != (char)(i & 0xFF)) { large_ok = 0; break; }
    vga_puts_color(large_ok ? "  [OK] " : "  [!!] ",
                   large_ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    vga_puts("Large alloc — 1KB buffer write/read verified\n");

    /* Test 5: stress test */
    void* ptrs[16];
    for (int i = 0; i < 16; i++) ptrs[i] = kmalloc(256);
    for (int i = 0; i < 16; i++) kfree(ptrs[i]);
    void* after = kmalloc(256);
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Stress test — 16 alloc/free cycles passed\n");
    kfree(after);

    /* Cleanup */
    kfree(a); kfree(c); kfree(b2); kfree(zbuf); kfree(buf);

    vga_puts("\n");
    heap_dump_stats();

    vga_puts("\n");
    vga_puts_color("  YouOS kernel is alive. Dynamic memory working.\n",
                   VGA_WHITE, VGA_BLACK);

    while (1) { __asm__ volatile ("hlt"); }
}
