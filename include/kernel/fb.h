#ifndef KERNEL_FB_H
#define KERNEL_FB_H

#include <stdint.h>

typedef struct {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
} fb_info_t;

void fb_init(uint64_t addr, uint32_t width, uint32_t height,
             uint32_t pitch, uint8_t bpp);
int  fb_available(void);
void fb_fill(uint32_t color);
void fb_put_pixel(int x, int y, uint32_t color);
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(int x, int y, const char* s, uint32_t fg, uint32_t bg);
void fb_terminal_init(void);
void fb_terminal_putchar(char c);
void fb_terminal_puts(const char* s);
void fb_terminal_puts_color(const char* s, uint32_t fg, uint32_t bg);
void fb_terminal_set_color(uint32_t fg, uint32_t bg);
/* Pass address_space_t* as void* to avoid circular include */
void fb_map_into_as(void* as);
fb_info_t* fb_get_info(void);
void fb_put_pixel_alpha(int x, int y, uint32_t color, uint8_t alpha);
void fb_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha);

#define FB_COLOR(r,g,b) (((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define FB_BLACK   0x000000
#define FB_WHITE   0xFFFFFF
#define FB_RED     0xFF4444
#define FB_GREEN   0x44FF44
#define FB_CYAN    0x44FFFF
#define FB_YELLOW  0xFFFF44
#define FB_GREY    0xAAAAAA
#define FB_DKGREY  0x333333
#define FB_BLUE    0x4444FF
#define FB_ORANGE  0xFF8844

#endif
