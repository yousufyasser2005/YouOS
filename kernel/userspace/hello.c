/*
 * YouOS - First "Userspace" Program
 * Runs in kernel mode for now, uses syscall interface
 * True Ring 3 requires user page tables (next step)
 */

#include <kernel/vga.h>
#include <kernel/process.h>

/* Simulate syscall write */
static void sys_write_sim(const char* s) {
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts(s);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void print_int(int n) {
    char buf[16]; int i = 15; buf[i] = '\0';
    if (n == 0) { vga_putchar('0'); return; }
    while (n > 0) { buf[--i] = '0' + (n % 10); n /= 10; }
    vga_puts(&buf[i]);
}

void hello_main(void)
{
    sys_write_sim("Hello from YouOS userspace program!\n");
    sys_write_sim("Syscall interface: SYSCALL/SYSRET ready\n");

    sys_write_sim("My PID is: ");
    print_int(process_current()->pid);
    sys_write_sim("\n");

    sys_write_sim("Calling yield syscall...\n");
    process_yield();
    sys_write_sim("Returned from yield!\n");

    sys_write_sim("Program complete. exit(0) called.\n");
}
