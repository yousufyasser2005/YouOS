#include "../../include/kernel/mouse.h"
#include "../../include/kernel/irq.h"
#include "../../include/kernel/idt.h"
#include "../../include/kernel/pic.h"
#include "../../include/kernel/fb.h"
static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static void ps2_wait_write(void) { int t=100000; while(--t&&(inb(0x64)&0x02)); }
static void ps2_wait_read(void)  { int t=100000; while(--t&&!(inb(0x64)&0x01)); }
static void mouse_write(unsigned char data) {
    ps2_wait_write(); outb(0x64,0xD4);
    ps2_wait_write(); outb(0x60,data);
}
static unsigned char mouse_read_byte(void) {
    ps2_wait_read(); return inb(0x60);
}
static int mouse_x=512, mouse_y=384, mouse_buttons=0;
/* Real screen bounds, set once in mouse_init() from fb_get_info() --
 * this used to be a hardcoded 1023/767 (1024x768) everywhere below,
 * silently wrong for this project's actual 1280x800 framebuffer (see
 * "Framebuffer: 1280x800x32bpp" in the boot log). The practical effect:
 * the cursor could never move past x=1023, permanently unreachable
 * short of the real screen's right ~257 pixels -- exactly where
 * desktop.c's sidebar widget panel lives, so it looked like "the mouse
 * can't move over the widget panel" when actually the cursor's own
 * position was silently clamped short of it the whole time. Defaults
 * to the old 1023/767 here only as a fallback in case mouse_init() is
 * somehow never called before the first IRQ (shouldn't happen --
 * fb_init() always runs first, see kernel_main.c) -- never actually
 * exercised as a hardcoded value once mouse_init() runs. */
static int mouse_max_x=1023, mouse_max_y=767;
static int mouse_dbg_len=0, mouse_dbg_byte3=0;
static int mouse_has_wheel=0, mouse_wheel_delta=0;
static unsigned char mouse_cycle=0, mouse_bytes[4];
static void mouse_irq_handler(registers_t *regs) {
    (void)regs;
    unsigned char data = inb(0x60);
    switch(mouse_cycle){
    case 0:
        if(!(data & 0x08)) return;
        mouse_bytes[0]=data; mouse_cycle=1; break;
    case 1:
        mouse_bytes[1]=data; mouse_cycle=2; break;
    case 2: {
        mouse_bytes[2]=data; mouse_cycle = mouse_has_wheel ? 3 : 0;
        if(mouse_has_wheel) break;
        if(mouse_bytes[0] & 0xC0) break;
        mouse_buttons = mouse_bytes[0] & 0x07;
        int dx = (int)mouse_bytes[1];
        if(mouse_bytes[0] & 0x10) dx -= 256;
        int dy = (int)mouse_bytes[2];
        if(mouse_bytes[0] & 0x20) dy -= 256;
        mouse_x += dx;
        mouse_y -= dy;
        if(mouse_x <    0) mouse_x =    0;
        if(mouse_x > mouse_max_x) mouse_x = mouse_max_x;
        if(mouse_y <    0) mouse_y =    0;
        if(mouse_y > mouse_max_y) mouse_y = mouse_max_y;
        break;
    }
    case 3: {
        mouse_bytes[3]=data; mouse_cycle=0;
        if(mouse_bytes[0] & 0xC0) break;
        mouse_buttons = mouse_bytes[0] & 0x07;
        int dx = (int)mouse_bytes[1];
        if(mouse_bytes[0] & 0x10) dx -= 256;
        int dy = (int)mouse_bytes[2];
        if(mouse_bytes[0] & 0x20) dy -= 256;
        mouse_x += dx;
        mouse_y -= dy;
        if(mouse_x <    0) mouse_x =    0;
        if(mouse_x > mouse_max_x) mouse_x = mouse_max_x;
        if(mouse_y <    0) mouse_y =    0;
        if(mouse_y > mouse_max_y) mouse_y = mouse_max_y;
        signed char wz = (signed char)mouse_bytes[3];
        mouse_wheel_delta += (int)wz;
        break;
    }
    }
}
int mouse_get_x(void)       { return mouse_x;       }
int mouse_get_y(void)       { return mouse_y;       }
int mouse_get_buttons(void) { return mouse_buttons; }
void mouse_set_debug_report(int len, int byte3) { mouse_dbg_len=len; mouse_dbg_byte3=byte3; }
int  mouse_get_debug_len(void)   { return mouse_dbg_len; }
int  mouse_get_debug_byte3(void) { return mouse_dbg_byte3; }
int mouse_get_wheel_delta(void) {
    int d = mouse_wheel_delta;
    mouse_wheel_delta = 0;
    return d;
}
int mouse_has_wheel_support(void) { return mouse_has_wheel; }
void mouse_init(void) {
    /* Real screen bounds, not the old hardcoded 1024x768 guess -- see
     * mouse_max_x/mouse_max_y's own comment above. fb_init() always
     * runs before mouse_init() (kernel_main.c), so fb_get_info() is
     * guaranteed valid here. */
    fb_info_t* fb = fb_get_info();
    if (fb && fb->width > 0 && fb->height > 0) {
        mouse_max_x = (int)fb->width  - 1;
        mouse_max_y = (int)fb->height - 1;
    }

    ps2_wait_write(); outb(0x64, 0xA8);
    ps2_wait_write(); outb(0x64, 0x20);
    unsigned char cfg = mouse_read_byte();
    cfg |=  0x02;
    cfg &= ~0x20;
    ps2_wait_write(); outb(0x64, 0x60);
    ps2_wait_write(); outb(0x60, cfg);
    mouse_write(0xFF);
    mouse_read_byte();
    mouse_read_byte();
    mouse_read_byte();

    /* IntelliMouse wheel-enable handshake: 0xF3 + rate, sequence 200,100,80 */
    mouse_write(0xF3); mouse_read_byte(); mouse_write(200); mouse_read_byte();
    mouse_write(0xF3); mouse_read_byte(); mouse_write(100); mouse_read_byte();
    mouse_write(0xF3); mouse_read_byte(); mouse_write(80);  mouse_read_byte();

    mouse_write(0xF2); mouse_read_byte();
    unsigned char dev_id = mouse_read_byte();
    if (dev_id == 0x03) mouse_has_wheel = 1;

    mouse_write(0xF6);
    mouse_read_byte();
    mouse_write(0xF4);
    mouse_read_byte();
    irq_set_handler(12, mouse_irq_handler);
    pic_unmask(2);
    pic_unmask(12);
}
void mouse_update_usb_delta(int dx, int dy, int buttons) {
    mouse_x += dx; mouse_y -= dy;
    if (mouse_x < 0)    mouse_x = 0;
    if (mouse_x > mouse_max_x)  mouse_x = mouse_max_x;
    if (mouse_y < 0)    mouse_y = 0;
    if (mouse_y > mouse_max_y)  mouse_y = mouse_max_y;
    mouse_buttons = buttons & 0x07;
}
