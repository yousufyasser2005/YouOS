#include "../../include/kernel/mouse.h"
#include "../../include/kernel/irq.h"
#include "../../include/kernel/idt.h"
#include "../../include/kernel/pic.h"
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
static unsigned char mouse_cycle=0, mouse_bytes[3];
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
        mouse_bytes[2]=data; mouse_cycle=0;
        if(mouse_bytes[0] & 0xC0) break;
        mouse_buttons = mouse_bytes[0] & 0x07;
        int dx = (int)mouse_bytes[1];
        if(mouse_bytes[0] & 0x10) dx -= 256;
        int dy = (int)mouse_bytes[2];
        if(mouse_bytes[0] & 0x20) dy -= 256;
        mouse_x += dx;
        mouse_y -= dy;
        if(mouse_x <    0) mouse_x =    0;
        if(mouse_x > 1023) mouse_x = 1023;
        if(mouse_y <    0) mouse_y =    0;
        if(mouse_y >  767) mouse_y =  767;
        break;
    }
    }
}
int mouse_get_x(void)       { return mouse_x;       }
int mouse_get_y(void)       { return mouse_y;       }
int mouse_get_buttons(void) { return mouse_buttons; }
void mouse_init(void) {
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
    if(mouse_x<0)mouse_x=0; if(mouse_x>1023)mouse_x=1023;
    if(mouse_y<0)mouse_y=0; if(mouse_y>767)mouse_y=767;
    mouse_buttons = buttons & 0x07;
}
