#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H
#include <stdint.h>

#define SYS_EXIT       0
#define SYS_WRITE      1
#define SYS_READ       2
#define SYS_GETPID     3
#define SYS_YIELD      4
#define SYS_SLEEP      5
#define SYS_FBINFO     12
#define SYS_FBWRITE    13
#define SYS_KEYPOLL    14
#define SYS_TICKS      15
#define SYS_MOUSEREAD  16
#define SYS_READDIR    17
#define SYS_SAVEFILE   18

#define SYS_STAT    19
#define SYS_MKDIR   20
#define SYS_UNLINK  21
#define SYS_MSGPOST  22
#define SYS_MSGRECV  23
#define SYS_MQCREATE 24
#define SYS_RENAME   25
#define SYS_READDIR2 26
#define SYS_READCRASH  27
#define SYS_READSYSLOG 28
#define SYS_MOUSEDBG    29
#define SYS_MOUSEWHEEL  30
#define SYS_SET_SESSION_UID 31
#define SYS_YOUDO 32
#define SYS_CHMOD 33
#define SYS_CHOWN 34
#define SYS_FILEINFO 35
#define SYS_PLAY_PCM 36
#define SYS_PCM_DONE 37
#define SYS_AC97_DEBUG 38
#define SYS_PCM_CAN_SUBMIT 39
#define SYS_PLAY_STREAM 40
#define SYS_STREAM_ACTIVE 41
#define SYS_GET_EXEC_ARG 42

/* Non-blocking spawn/wait pair -- Phase 1 of true concurrent
 * multi-program execution. sys_spawn() is like sys_exec() but returns
 * the child's pid immediately instead of blocking; sys_wait_nonblock()
 * checks (and reaps, if dead) a pid previously returned by sys_spawn()
 * without ever blocking. See spawn_common()/sys_spawn()/
 * sys_wait_nonblock() in syscall.c for the full contract. */
#define SYS_SPAWN         43
#define SYS_WAIT_NONBLOCK 44

/* Forcibly terminates another process -- see process_kill()'s comment
 * in scheduler.c for the full contract. */
#define SYS_KILL          45

/* Free physical page count -- see sys_meminfo()'s comment in
 * syscall.c. */
#define SYS_MEMINFO       46

/* Total physical page count -- pairs with SYS_MEMINFO so userspace can
 * compute a real used/total percentage (desktop's "MEM" sidebar stat
 * was, until now, a hardcoded string -- see sys_mem_total()'s comment
 * in syscall.c). */
#define SYS_MEM_TOTAL     47

/* Live CPU-busy percentage (0-100) for the interval since the previous
 * call -- see scheduler_get_cpu_percent()'s comment in scheduler.c for
 * how this is actually measured (a real idle task, not a since-boot
 * average). Added alongside SYS_MEM_TOTAL for the same reason: desktop's
 * "CPU" sidebar stat was also a hardcoded string. */
#define SYS_CPUINFO       48

#define SYSCALL_COUNT  49

void syscall_init(void);
#endif
