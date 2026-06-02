#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H
#include <stdint.h>

#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_READ      2
#define SYS_GETPID    3
#define SYS_YIELD     4
#define SYS_SLEEP     5
#define SYS_FBINFO    12
#define SYS_FBWRITE   13
#define SYS_KEYPOLL   14
#define SYS_TICKS     15
#define SYS_MOUSEREAD 16
#define SYS_READDIR   17

#define SYSCALL_COUNT 18

void syscall_init(void);
#endif
