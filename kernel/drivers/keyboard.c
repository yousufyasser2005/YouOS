#include <kernel/keyboard.h>
#include <kernel/irq.h>
#include <kernel/idt.h>
#include <kernel/pic.h>

static const char scancode_ascii[] = {
    0,    0,   '1', '2', '3', '4', '5', '6',
    '7', '8', '9', '0', '-', '=',  0,   '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', 0,  'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'','`',  0,  '\\','z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/',  0,   '*',
    0,   ' ',  0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   '7',
    '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.',  0,   0,   0,   0,
};
static const char scancode_ascii_shift[] = {
    0,    0,   '!', '@', '#', '$', '%', '^',
    '&', '*', '(', ')', '_', '+',  0,   '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0,  'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~',  0,  '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?',  0,   '*',
    0,   ' ',  0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   '7',
    '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.',  0,   0,   0,   0,
};
#define SCANCODE_MAX (sizeof(scancode_ascii)/sizeof(scancode_ascii[0]))

static key_event_t       kb_buffer[KB_BUFFER_SIZE];
static volatile uint32_t kb_head = 0;
static volatile uint32_t kb_tail = 0;

static void kb_push(key_event_t* e) {
    uint32_t n = (kb_head+1)%KB_BUFFER_SIZE;
    if (n!=kb_tail) { kb_buffer[kb_head]=*e; kb_head=n; }
}
static int kb_pop(key_event_t* e) {
    if (kb_head==kb_tail) return 0;
    *e=kb_buffer[kb_tail];
    kb_tail=(kb_tail+1)%KB_BUFFER_SIZE;
    return 1;
}

static uint8_t shift_held=0, ctrl_held=0, alt_held=0, capslock_on=0;
static void (*key_callback)(key_event_t*)=0;

static inline uint8_t inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v;
}

static void kb_irq(registers_t* regs) {
    (void)regs;
    uint8_t sc=inb(KB_DATA_PORT), down=!(sc&0x80), code=sc&0x7F;
    if(code==KEY_LSHIFT||code==KEY_RSHIFT){shift_held=down;return;}
    if(code==KEY_LCTRL){ctrl_held=down;return;}
    if(code==KEY_LALT){alt_held=down;return;}
    if(code==KEY_CAPSLOCK&&down){capslock_on=!capslock_on;return;}
    if(!down) return;
    key_event_t e;
    e.scancode=code; e.pressed=down;
    e.shift=shift_held; e.ctrl=ctrl_held;
    e.alt=alt_held; e.capslock=capslock_on; e.ascii=0;
    if(code<SCANCODE_MAX){
        uint8_t up=shift_held^capslock_on;
        e.ascii=up?scancode_ascii_shift[code]:scancode_ascii[code];
    }
    if(code==KEY_BACKSPACE) e.ascii='\b';
    kb_push(&e);
    if(key_callback) key_callback(&e);
}

void keyboard_init(void) {
    while(inb(KB_STATUS_PORT)&0x01) inb(KB_DATA_PORT);
    irq_set_handler(IRQ_KEYBOARD, kb_irq);
    pic_unmask(IRQ_KEYBOARD);
}

int  keyboard_get_event(key_event_t* e) { return kb_pop(e); }
int  keyboard_available(void)           { return kb_head!=kb_tail; }
void keyboard_set_callback(void(*cb)(key_event_t*)) { key_callback=cb; }

char keyboard_getchar(void) {
    key_event_t e;
    while (1) {
        if (kb_pop(&e) && e.ascii) return e.ascii;
        __asm__ volatile ("hlt");
    }
}
