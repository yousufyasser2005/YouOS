#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stdint.h>

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_GETPID  3
#define SYS_YIELD   4
#define SYS_SLEEP   5
#define SYSCALL_COUNT 14

void syscall_init(void);

#endif

/* Framebuffer syscalls */
#define SYS_FBINFO   12   /* get fb info: addr,w,h,pitch,bpp */
#define SYS_FBWRITE  13   /* write pixels: x,y,w,h,data_ptr */
