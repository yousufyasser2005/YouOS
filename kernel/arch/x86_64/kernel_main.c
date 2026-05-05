#include <stdint.h>
#include <stddef.h>
#include <kernel/gdt.h>

/* =========================================================================
 * VGA Text Mode
 * ========================================================================= */
#define VGA_ADDRESS  0xB8000
#define VGA_COLS     80
#define VGA_ROWS     25

typedef enum {
    VGA_BLACK=0, VGA_BLUE=1, VGA_GREEN=2, VGA_CYAN=3,
    VGA_RED=4, VGA_MAGENTA=5, VGA_BROWN=6, VGA_LIGHT_GREY=7,
    VGA_DARK_GREY=8, VGA_LIGHT_BLUE=9, VGA_LIGHT_GREEN=10,
    VGA_LIGHT_CYAN=11, VGA_LIGHT_RED=12, VGA_LIGHT_MAGENTA=13,
    VGA_YELLOW=14, VGA_WHITE=15,
} vga_color_t;

static inline uint8_t  vga_color(vga_color_t fg, vga_color_t bg) { return fg | (bg << 4); }
static inline uint16_t vga_entry(char c, uint8_t color) { return (uint16_t)c | ((uint16_t)color << 8); }

static uint16_t* vga_buf  = (uint16_t*)VGA_ADDRESS;
static uint8_t   vga_col  = 0;
static uint8_t   vga_row  = 0;
static uint8_t   vga_attr = 0;

static void vga_clear(void) {
    uint8_t color = vga_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (size_t r = 0; r < VGA_ROWS; r++)
        for (size_t c = 0; c < VGA_COLS; c++)
            vga_buf[r * VGA_COLS + c] = vga_entry(' ', color);
    vga_row = vga_col = 0;
}

static void vga_scroll(void) {
    for (size_t r = 1; r < VGA_ROWS; r++)
        for (size_t c = 0; c < VGA_COLS; c++)
            vga_buf[(r-1) * VGA_COLS + c] = vga_buf[r * VGA_COLS + c];
    for (size_t c = 0; c < VGA_COLS; c++)
        vga_buf[(VGA_ROWS-1) * VGA_COLS + c] = vga_entry(' ', vga_attr);
    vga_row = VGA_ROWS - 1;
}

static void vga_putchar(char c) {
    if (c == '\n') { vga_col = 0; vga_row++; if (vga_row >= VGA_ROWS) vga_scroll(); return; }
    vga_buf[vga_row * VGA_COLS + vga_col] = vga_entry(c, vga_attr);
    if (++vga_col >= VGA_COLS) { vga_col = 0; if (++vga_row >= VGA_ROWS) vga_scroll(); }
}

static void vga_puts(const char* s) { while (*s) vga_putchar(*s++); }

static void vga_puts_color(const char* s, vga_color_t fg, vga_color_t bg) {
    uint8_t saved = vga_attr;
    vga_attr = vga_color(fg, bg);
    vga_puts(s);
    vga_attr = saved;
}

/* =========================================================================
 * kernel_main
 * ========================================================================= */
#define MULTIBOOT2_MAGIC 0x36D76289

void kernel_main(uint32_t mb2_magic, uint32_t mb2_info) {
    (void)mb2_info;

    vga_attr = vga_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();

    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts_color("                             Welcome to YouOS                                  \n", VGA_YELLOW,     VGA_BLACK);
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");

    /* Multiboot2 check */
    if (mb2_magic == MULTIBOOT2_MAGIC) {
        vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("Multiboot2 bootloader detected\n");
    } else {
        vga_puts_color("  [!!] ", VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("WARNING: Invalid Multiboot2 magic!\n");
    }

    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("VGA text mode initialized (80x25)\n");

    /* Initialize GDT */
    gdt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("GDT loaded — kernel/user segments + TSS ready\n");

    vga_puts("\n");
    vga_puts_color("  YouOS kernel is alive. GDT active.\n", VGA_WHITE, VGA_BLACK);
    vga_puts("\n");

    while (1) { __asm__ volatile ("hlt"); }
}
