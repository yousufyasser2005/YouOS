/*
 * YouOS - First Userspace Program
 * hello.c
 *
 * This runs in Ring 3 (userspace) and uses syscalls
 * to communicate with the kernel.
 */

#include <user/syscall.h>

void hello_main(void)
{
    puts_user("Hello from YouOS userspace!\n");
    puts_user("Running in Ring 3 — syscalls work!\n");

    int pid = getpid();
    puts_user("My PID is: ");
    /* Simple int to string */
    char buf[16];
    int i = 15; buf[i] = '\0';
    int p = pid;
    if (p == 0) { buf[--i] = '0'; }
    else while (p > 0) { buf[--i] = '0' + (p % 10); p /= 10; }
    puts_user(&buf[i]);
    puts_user("\n");

    puts_user("Calling yield...\n");
    yield();
    puts_user("Returned from yield!\n");

    puts_user("Exiting userspace program.\n");
    exit(0);
}
