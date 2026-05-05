#include <stdint.h>
#include <stddef.h>
#include <kernel/vga.h>

#define VGA_ADDRESS 0xB8000
#define VGA_COLS    80
#define VGA_ROWS    25

static inline uint8_t  mk_color(vga_color_t fg, vga_color_t bg) { return fg | (bg << 4); }
static inline uint16_t mk_entry(char c, uint8_t color) { return (uint16_t)c | ((uint16_t)color << 8); }

static uint16_t* buf  = (uint16_t*)VGA_ADDRESS;
static uint8_t   col  = 0;
static uint8_t   row  = 0;
static uint8_t   attr = 0;

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

void vga_putchar(char c) {
    if (c == '\n') { col = 0; if (++row >= VGA_ROWS) scroll(); return; }
    buf[row * VGA_COLS + col] = mk_entry(c, attr);
    if (++col >= VGA_COLS) { col = 0; if (++row >= VGA_ROWS) scroll(); }
}

void vga_puts(const char* s) { while (*s) vga_putchar(*s++); }

void vga_set_color(vga_color_t fg, vga_color_t bg) { attr = mk_color(fg, bg); }

void vga_puts_color(const char* s, vga_color_t fg, vga_color_t bg) {
    uint8_t saved = attr;
    attr = mk_color(fg, bg);
    vga_puts(s);
    attr = saved;
}
