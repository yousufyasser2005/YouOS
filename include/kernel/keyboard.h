#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include <stdint.h>

/*
 * PS/2 Keyboard I/O ports
 */
#define KB_DATA_PORT    0x60    /* Read scancode / write command */
#define KB_STATUS_PORT  0x64    /* Read status / write command   */

/*
 * Special key codes
 */
#define KEY_UNKNOWN     0x00
#define KEY_ESCAPE      0x01
#define KEY_BACKSPACE   0x0E
#define KEY_TAB         0x0F
#define KEY_ENTER       0x1C
#define KEY_LCTRL       0x1D
#define KEY_LSHIFT      0x2A
#define KEY_RSHIFT      0x36
#define KEY_LALT        0x38
#define KEY_CAPSLOCK    0x3A
#define KEY_F1          0x3B
#define KEY_F2          0x3C
#define KEY_F3          0x3D
#define KEY_F4          0x3E
#define KEY_F5          0x3F
#define KEY_F6          0x40
#define KEY_F7          0x41
#define KEY_F8          0x42
#define KEY_F9          0x43
#define KEY_F10         0x44
#define KEY_UP          0x48
#define KEY_LEFT        0x4B
#define KEY_RIGHT       0x4D
#define KEY_DOWN        0x50
#define KEY_DELETE      0x53

/* Key event */
typedef struct {
    uint8_t scancode;   /* Raw scancode from keyboard   */
    char    ascii;      /* ASCII char (0 if non-printable) */
    uint8_t pressed;    /* 1 = key down, 0 = key up     */
    uint8_t shift;      /* Shift held                   */
    uint8_t ctrl;       /* Ctrl held                    */
    uint8_t alt;        /* Alt held                     */
    uint8_t capslock;   /* Capslock active              */
} key_event_t;

/* Keyboard input buffer size */
#define KB_BUFFER_SIZE  256

/* Initialize the keyboard driver */
void keyboard_init(void);

/* Get next key event (returns 0 if buffer empty) */
int keyboard_get_event(key_event_t* event);

/* Wait for and return next ASCII character (blocking) */
char keyboard_getchar(void);

/* Check if a key is available in buffer */
int keyboard_available(void);

/* Set a callback for key events */
void keyboard_set_callback(void (*cb)(key_event_t*));

#endif /* KERNEL_KEYBOARD_H */
