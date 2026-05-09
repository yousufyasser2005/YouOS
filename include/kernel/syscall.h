#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <stdint.h>

/*
 * YouOS Syscall Numbers
 */
#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_READ        2
#define SYS_GETPID      3
#define SYS_YIELD       4
#define SYS_SLEEP       5
#define SYS_SBRK        6

#define SYSCALL_COUNT   7

/*
 * Syscall calling convention (x86_64):
 *   rax = syscall number
 *   rdi = arg1
 *   rsi = arg2
 *   rdx = arg3
 *   r10 = arg4
 *   r8  = arg5
 *   r9  = arg6
 *   return value in rax
 */

/* Initialize syscall interface (sets up SYSCALL/SYSRET MSRs) */
void syscall_init(void);

#endif
