/*
 * YouOS - Terminal Driver
 * terminal.c
 *
 * Combines keyboard input + VGA output into an
 * interactive terminal with line editing.
 */

#include <kernel/terminal.h>
#include <kernel/keyboard.h>
#include <kernel/vga.h>

#define LINE_MAX 256

void terminal_init(void)
{
    keyboard_init();
}

void terminal_readline(char* buf, size_t max)
{
    size_t pos = 0;

    while (1) {
        char c = keyboard_getchar();

        if (c == '\n' || c == '\r') {
            buf[pos] = '\0';
            vga_putchar('\n');
            return;
        }

        if (c == '\b') {
            if (pos > 0) {
                pos--;
                /* Erase character on screen */
                vga_putchar('\b');
                vga_putchar(' ');
                vga_putchar('\b');
            }
            continue;
        }

        /* Printable character */
        if (pos < max - 1) {
            buf[pos++] = c;
            vga_putchar(c);
        }
    }
}

/* =========================================================================
 * kprintf — minimal kernel printf
 * Supports: %s %c %d %u %x %X %%
 * ========================================================================= */
static void kprintf_puts(const char* s) {
    while (*s) vga_putchar(*s++);
}

static void kprintf_putchar(char c) {
    vga_putchar(c);
}

static void kprintf_uint(uint64_t val, int base, int upper) {
    const char* digits = upper ? "0123456789ABCDEF"
                               : "0123456789abcdef";
    char buf[64];
    int  i = 63;
    buf[i] = '\0';
    if (val == 0) { vga_putchar('0'); return; }
    while (val > 0) {
        buf[--i] = digits[val % base];
        val /= base;
    }
    kprintf_puts(&buf[i]);
}

static void kprintf_int(int64_t val) {
    if (val < 0) { vga_putchar('-'); val = -val; }
    kprintf_uint((uint64_t)val, 10, 0);
}

/* va_list implementation for freestanding environment */
typedef __builtin_va_list va_list;
#define va_start(v,l)   __builtin_va_start(v,l)
#define va_end(v)       __builtin_va_end(v)
#define va_arg(v,l)     __builtin_va_arg(v,l)

void kprintf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            vga_putchar(*fmt++);
            continue;
        }
        fmt++;  /* skip '%' */

        switch (*fmt) {
            case 's': kprintf_puts(va_arg(args, const char*)); break;
            case 'c': kprintf_putchar((char)va_arg(args, int)); break;
            case 'd': kprintf_int((int64_t)va_arg(args, int)); break;
            case 'u': kprintf_uint((uint64_t)va_arg(args, unsigned int), 10, 0); break;
            case 'x': kprintf_puts("0x");
                      kprintf_uint((uint64_t)va_arg(args, unsigned int), 16, 0); break;
            case 'X': kprintf_puts("0x");
                      kprintf_uint((uint64_t)va_arg(args, unsigned int), 16, 1); break;
            case 'l': fmt++;
                      if (*fmt == 'u')
                          kprintf_uint(va_arg(args, uint64_t), 10, 0);
                      else if (*fmt == 'x')
                          kprintf_uint(va_arg(args, uint64_t), 16, 0);
                      else if (*fmt == 'd')
                          kprintf_int(va_arg(args, int64_t));
                      break;
            case '%': vga_putchar('%'); break;
            default:  vga_putchar('%'); vga_putchar(*fmt); break;
        }
        fmt++;
    }

    va_end(args);
}
