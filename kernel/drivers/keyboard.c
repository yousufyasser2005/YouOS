/*
 * YouOS - PS/2 Keyboard Driver
 * keyboard.c
 *
 * Handles IRQ1, translates scancodes to ASCII,
 * stores events in a circular ring buffer.
 */

#include <kernel/keyboard.h>
#include <kernel/irq.h>
#include <kernel/idt.h>
#include <kernel/pic.h>

/* =========================================================================
 * Scancode → ASCII translation tables
 * Index = scancode, value = ASCII character
 * ========================================================================= */

/* Normal (no shift) */
static const char scancode_ascii[] = {
    0,    0,   '1', '2', '3', '4', '5', '6',   /* 0x00 - 0x07 */
    '7', '8', '9', '0', '-', '=',  0,   '\t',  /* 0x08 - 0x0F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',   /* 0x10 - 0x17 */
    'o', 'p', '[', ']', '\n', 0,  'a', 's',   /* 0x18 - 0x1F */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',   /* 0x20 - 0x27 */
    '\'','`',  0,  '\\','z', 'x', 'c', 'v',   /* 0x28 - 0x2F */
    'b', 'n', 'm', ',', '.', '/',  0,   '*',   /* 0x30 - 0x37 */
    0,   ' ',  0,   0,   0,   0,   0,   0,    /* 0x38 - 0x3F */
    0,   0,   0,   0,   0,   0,   0,   '7',   /* 0x40 - 0x47 */
    '8', '9', '-', '4', '5', '6', '+', '1',   /* 0x48 - 0x4F */
    '2', '3', '0', '.',  0,   0,   0,   0,    /* 0x50 - 0x57 */
};

/* Shifted */
static const char scancode_ascii_shift[] = {
    0,    0,   '!', '@', '#', '$', '%', '^',   /* 0x00 - 0x07 */
    '&', '*', '(', ')', '_', '+',  0,   '\t',  /* 0x08 - 0x0F */
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',   /* 0x10 - 0x17 */
    'O', 'P', '{', '}', '\n', 0,  'A', 'S',   /* 0x18 - 0x1F */
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',   /* 0x20 - 0x27 */
    '"', '~',  0,  '|', 'Z', 'X', 'C', 'V',   /* 0x28 - 0x2F */
    'B', 'N', 'M', '<', '>', '?',  0,   '*',   /* 0x30 - 0x37 */
    0,   ' ',  0,   0,   0,   0,   0,   0,    /* 0x38 - 0x3F */
    0,   0,   0,   0,   0,   0,   0,   '7',   /* 0x40 - 0x47 */
    '8', '9', '-', '4', '5', '6', '+', '1',   /* 0x48 - 0x4F */
    '2', '3', '0', '.',  0,   0,   0,   0,    /* 0x50 - 0x57 */
};

#define SCANCODE_MAX (sizeof(scancode_ascii) / sizeof(scancode_ascii[0]))

/* =========================================================================
 * Ring buffer for key events
 * ========================================================================= */
static key_event_t kb_buffer[KB_BUFFER_SIZE];
static volatile uint32_t kb_head = 0;   /* write index */
static volatile uint32_t kb_tail = 0;   /* read index  */

static void kb_buffer_push(key_event_t* e) {
    uint32_t next = (kb_head + 1) % KB_BUFFER_SIZE;
    if (next != kb_tail) {   /* not full */
        kb_buffer[kb_head] = *e;
        kb_head = next;
    }
}

static int kb_buffer_pop(key_event_t* e) {
    if (kb_head == kb_tail) return 0;   /* empty */
    *e = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    return 1;
}

/* =========================================================================
 * Keyboard state
 * ========================================================================= */
static uint8_t shift_held    = 0;
static uint8_t ctrl_held     = 0;
static uint8_t alt_held      = 0;
static uint8_t capslock_on   = 0;

/* Optional callback */
static void (*key_callback)(key_event_t*) = 0;

/* =========================================================================
 * Port I/O
 * ========================================================================= */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* =========================================================================
 * IRQ1 handler — called on every keypress/release
 * ========================================================================= */
static void keyboard_irq_handler(registers_t* regs)
{
    (void)regs;

    uint8_t scancode = inb(KB_DATA_PORT);
    uint8_t pressed  = !(scancode & 0x80);
    uint8_t code     = scancode & 0x7F;

    /* Update modifier state */
    if (code == KEY_LSHIFT || code == KEY_RSHIFT) {
        shift_held = pressed;
        return;
    }
    if (code == KEY_LCTRL) { ctrl_held = pressed; return; }
    if (code == KEY_LALT)  { alt_held  = pressed; return; }
    if (code == KEY_CAPSLOCK && pressed) {
        capslock_on = !capslock_on;
        return;
    }

    /* Only queue key-down events with printable ASCII */
    if (!pressed) return;

    key_event_t event;
    event.scancode = code;
    event.pressed  = pressed;
    event.shift    = shift_held;
    event.ctrl     = ctrl_held;
    event.alt      = alt_held;
    event.capslock = capslock_on;
    event.ascii    = 0;

    /* Translate scancode to ASCII */
    if (code < SCANCODE_MAX) {
        uint8_t use_upper = shift_held ^ capslock_on;
        char c = use_upper ? scancode_ascii_shift[code]
                           : scancode_ascii[code];
        event.ascii = c;
    }

    /* Handle backspace specially */
    if (code == KEY_BACKSPACE) event.ascii = '\b';

    /* Push to buffer */
    kb_buffer_push(&event);

    /* Call callback if registered */
    if (key_callback) key_callback(&event);
}

/* =========================================================================
 * Public API
 * ========================================================================= */
void keyboard_init(void)
{
    /* Flush any pending data */
    while (inb(KB_STATUS_PORT) & 0x01) inb(KB_DATA_PORT);

    /* Register IRQ1 handler and unmask it */
    irq_set_handler(IRQ_KEYBOARD, keyboard_irq_handler);
    pic_unmask(IRQ_KEYBOARD);
}

int keyboard_get_event(key_event_t* event)
{
    return kb_buffer_pop(event);
}

int keyboard_available(void)
{
    return kb_head != kb_tail;
}

char keyboard_getchar(void)
{
    key_event_t event;
    /* Spin until a key with ASCII value is available */
    while (1) {
        if (kb_buffer_pop(&event)) {
            if (event.ascii) return event.ascii;
        }
        __asm__ volatile ("hlt");
    }
}

void keyboard_set_callback(void (*cb)(key_event_t*))
{
    key_callback = cb;
}
