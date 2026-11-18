#ifndef KERNEL_KEYEV_H
#define KERNEL_KEYEV_H
#include <stdint.h>

/*
 * SYS_KEYEV: a key press WITH its modifier state (sys_keypoll() drops it, so Ctrl+C looked like 'c').
 * One 64-bit value, 0 = nothing pending:
 *   bits  0-15  key code, exactly what sys_keypoll() would return: the ASCII character, or
 *               1001 up, 1002 down, 1003 left, 1004 right, 1005 home, 1006 end, 1007 delete,
 *               1008 page up, 1009 page down; 0 for keys with no code (function keys, ...)
 *   bits 16-23  raw scancode
 *   bit  24     Shift held     bit 25  Ctrl held     bit 26  Alt held     bit 27  Caps Lock on
 *   bit  63     always set for a real event (so an event whose code is 0 is not mistaken for "none")
 * The driver queues presses only, so there is no release flag.
 */
#define KEYEV_SHIFT (1ull << 24)
#define KEYEV_CTRL  (1ull << 25)
#define KEYEV_ALT   (1ull << 26)
#define KEYEV_CAPS  (1ull << 27)
#define KEYEV_VALID (1ull << 63)

static inline uint32_t keyev_code_of(uint8_t scancode, char ascii) {
    switch (scancode) {
        case 0x48: return 1001; case 0x50: return 1002; case 0x4B: return 1003; case 0x4D: return 1004;
        case 0x47: return 1005; case 0x4F: return 1006; case 0x53: return 1007; case 0x49: return 1008;
        case 0x51: return 1009;
    }
    return ascii ? (uint32_t)(unsigned char)ascii : 0;
}
static inline uint64_t keyev_pack(uint8_t scancode, char ascii, int shift, int ctrl, int alt, int caps) {
    return KEYEV_VALID | (uint64_t)keyev_code_of(scancode, ascii) | ((uint64_t)scancode << 16)
         | (shift ? KEYEV_SHIFT : 0) | (ctrl ? KEYEV_CTRL : 0) | (alt ? KEYEV_ALT : 0) | (caps ? KEYEV_CAPS : 0);
}
#endif
