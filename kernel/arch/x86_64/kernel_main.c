/*
 * YouOS - First Kernel Entry (C)
 * kernel_main.c
 *
 * This is the first C code that runs in YouOS.
 * It prints a welcome message to the VGA text buffer.
 */

#include <stdint.h>
#include <stddef.h>

/* ==========================================================================
 * VGA Text Mode Driver (bare minimum)
 * VGA text buffer lives at physical address 0xB8000
 * Each cell = 2 bytes: [character][color]
 * ========================================================================== */

#define VGA_ADDRESS     0xB8000
#define VGA_COLS        80
#define VGA_ROWS        25

/* VGA colors */
typedef enum {
    VGA_BLACK         = 0,
    VGA_BLUE          = 1,
    VGA_GREEN         = 2,
    VGA_CYAN          = 3,
    VGA_RED           = 4,
    VGA_MAGENTA       = 5,
    VGA_BROWN         = 6,
    VGA_LIGHT_GREY    = 7,
    VGA_DARK_GREY     = 8,
    VGA_LIGHT_BLUE    = 9,
    VGA_LIGHT_GREEN   = 10,
    VGA_LIGHT_CYAN    = 11,
    VGA_LIGHT_RED     = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW        = 14,
    VGA_WHITE         = 15,
} vga_color_t;

static inline uint8_t vga_color(vga_color_t fg, vga_color_t bg) {
    return fg | (bg << 4);
}

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

/* VGA state */
static uint16_t* vga_buf   = (uint16_t*)VGA_ADDRESS;
static uint8_t   vga_col   = 0;
static uint8_t   vga_row   = 0;
static uint8_t   vga_attr  = 0;

/* Clear the screen */
static void vga_clear(void) {
    uint8_t color = vga_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (size_t row = 0; row < VGA_ROWS; row++) {
        for (size_t col = 0; col < VGA_COLS; col++) {
            vga_buf[row * VGA_COLS + col] = vga_entry(' ', color);
        }
    }
    vga_row = 0;
    vga_col = 0;
}

/* Scroll up one line */
static void vga_scroll(void) {
    for (size_t row = 1; row < VGA_ROWS; row++) {
        for (size_t col = 0; col < VGA_COLS; col++) {
            vga_buf[(row - 1) * VGA_COLS + col] = vga_buf[row * VGA_COLS + col];
        }
    }
    /* Clear last line */
    for (size_t col = 0; col < VGA_COLS; col++) {
        vga_buf[(VGA_ROWS - 1) * VGA_COLS + col] = vga_entry(' ', vga_attr);
    }
    vga_row = VGA_ROWS - 1;
}

/* Put a single character */
static void vga_putchar(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_ROWS) vga_scroll();
        return;
    }

    vga_buf[vga_row * VGA_COLS + vga_col] = vga_entry(c, vga_attr);
    vga_col++;

    if (vga_col >= VGA_COLS) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_ROWS) vga_scroll();
    }
}

/* Print a string */
static void vga_puts(const char* str) {
    while (*str) vga_putchar(*str++);
}

/* Print a string with a specific color */
static void vga_puts_color(const char* str, vga_color_t fg, vga_color_t bg) {
    uint8_t saved = vga_attr;
    vga_attr = vga_color(fg, bg);
    vga_puts(str);
    vga_attr = saved;
}

/* ==========================================================================
 * Multiboot2 Magic
 * ========================================================================== */
#define MULTIBOOT2_MAGIC 0x36D76289

/* ==========================================================================
 * kernel_main - First C function called by boot.asm
 * ========================================================================== */
void kernel_main(uint32_t mb2_magic, uint32_t mb2_info) {
    (void)mb2_info;   /* unused for now */

    /* Initialize VGA */
    vga_attr = vga_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();

    /* Banner */
    vga_puts_color(
        "=========================================="
        "======================================\n",
        VGA_LIGHT_CYAN, VGA_BLACK
    );

    vga_puts_color(
        "                          Welcome to YouOS"
        "                                \n",
        VGA_YELLOW, VGA_BLACK
    );

    vga_puts_color(
        "=========================================="
        "======================================\n",
        VGA_LIGHT_CYAN, VGA_BLACK
    );

    vga_puts("\n");

    /* Multiboot2 check */
    if (mb2_magic == MULTIBOOT2_MAGIC) {
        vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("Multiboot2 bootloader detected\n");
    } else {
        vga_puts_color("  [!!] ", VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("WARNING: Not loaded by a Multiboot2 bootloader!\n");
    }

    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("VGA text mode initialized (80x25)\n");

    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Kernel loaded at 0xFFFFFFFF80000000 (higher half)\n");

    vga_puts("\n");
    vga_puts_color(
        "  YouOS kernel is alive. Phase 1 begins.\n",
        VGA_WHITE, VGA_BLACK
    );
    vga_puts("\n");

    /* Hang forever - no interrupts yet */
    while (1) {
        __asm__ volatile ("hlt");
    }
}
