#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

#include <stdint.h>
#include <stddef.h>

/* Initialize terminal (keyboard + VGA) */
void terminal_init(void);

/* Read a line of input (blocking) */
void terminal_readline(char* buf, size_t max);

/* Print formatted output */
void kprintf(const char* fmt, ...);

#endif
