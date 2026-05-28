#include <stdint.h>
#include <stddef.h>
#include <kernel/vga.h>
#include <kernel/fb.h>

#define VGA_ADDRESS 0xB8000
#define VGA_COLS    80
#define VGA_ROWS    25

static inline uint8_t  mk_color(vga_color_t fg, vga_color_t bg) { return fg | (bg << 4); }
static inline uint16_t mk_entry(char c, uint8_t color) { return (uint16_t)c | ((uint16_t)color << 8); }

static uint16_t* buf  = (uint16_t*)VGA_ADDRESS;
static uint8_t   col  = 0;
static uint8_t   row  = 0;
static uint8_t   attr = 0;

/* Map VGA color index to 32-bit RGB for framebuffer */
static uint32_t vga_to_rgb(vga_color_t c) {
    static const uint32_t palette[16] = {
        0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
        0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
        0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
        0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
    };
    if ((int)c < 0 || c > 15) return 0xFFFFFF;
    return palette[c];
}

void vga_init(void) {
    attr = mk_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void vga_clear(void) {
    for (size_t r = 0; r < VGA_ROWS; r++)
        for (size_t c = 0; c < VGA_COLS; c++)
            buf[r * VGA_COLS + c] = mk_entry(' ', attr);
    row = col = 0;
}

static void scroll(void) {
    for (size_t r = 1; r < VGA_ROWS; r++)
        for (size_t c = 0; c < VGA_COLS; c++)
            buf[(r-1)*VGA_COLS+c] = buf[r*VGA_COLS+c];
    for (size_t c = 0; c < VGA_COLS; c++)
        buf[(VGA_ROWS-1)*VGA_COLS+c] = mk_entry(' ', attr);
    row = VGA_ROWS - 1;
}

static void vga_putchar_hw(char c) {
    if (c == '\n') { col = 0; if (++row >= VGA_ROWS) scroll(); return; }
    if (c == '\r') { col = 0; return; }
    if (c == '\b') {
        if (col > 0) col--;
        else if (row > 0) { row--; col = VGA_COLS - 1; }
        buf[row * VGA_COLS + col] = mk_entry(' ', attr);
        return;
    }
    buf[row * VGA_COLS + col] = mk_entry(c, attr);
    if (++col >= VGA_COLS) { col = 0; if (++row >= VGA_ROWS) scroll(); }
}

void vga_putchar(char c) {
    if (fb_available())
        fb_terminal_putchar(c);
    else
        vga_putchar_hw(c);
}

void vga_puts(const char* s) { while (*s) vga_putchar(*s++); }

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    if (fb_available())
        fb_terminal_set_color(vga_to_rgb(fg), vga_to_rgb(bg));
    attr = mk_color(fg, bg);
}

void vga_puts_color(const char* s, vga_color_t fg, vga_color_t bg) {
    if (fb_available()) {
        fb_terminal_puts_color(s, vga_to_rgb(fg), vga_to_rgb(bg));
        return;
    }
    uint8_t saved = attr;
    attr = mk_color(fg, bg);
    while (*s) vga_putchar_hw(*s++);
    attr = saved;
}
