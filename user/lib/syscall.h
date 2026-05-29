#pragma once
#include <stdint.h>

/* Syscall numbers */
#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_READ      2
#define SYS_GETPID    3
#define SYS_YIELD     4
#define SYS_SLEEP     5
#define SYS_OPEN      6
#define SYS_CLOSE     7
#define SYS_FREAD     8
#define SYS_SHUTDOWN  9
#define SYS_REBOOT    10
#define SYS_EXEC      11
#define SYS_FBINFO    12
#define SYS_FBWRITE   13

/* 6-argument syscall wrapper */
static inline uint64_t _sc(uint64_t n, uint64_t a, uint64_t b,
                            uint64_t c, uint64_t d, uint64_t e) {
    uint64_t r;
    register uint64_t r10 __asm__("r10") = d;
    register uint64_t r8  __asm__("r8")  = e;
    __asm__ volatile("syscall"
        : "=a"(r)
        : "0"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory");
    return r;
}

static inline void    sys_exit(int c)
    { _sc(SYS_EXIT, (uint64_t)c, 0, 0, 0, 0); }
static inline int64_t sys_write(int fd, const void* b, uint64_t l)
    { return (int64_t)_sc(SYS_WRITE, (uint64_t)fd, (uint64_t)b, l, 0, 0); }
static inline int64_t sys_read(int fd, void* b, uint64_t l)
    { return (int64_t)_sc(SYS_READ, (uint64_t)fd, (uint64_t)b, l, 0, 0); }
static inline int     sys_open(const char* p, int f)
    { return (int)_sc(SYS_OPEN, (uint64_t)p, (uint64_t)f, 0, 0, 0); }
static inline int     sys_close(int fd)
    { return (int)_sc(SYS_CLOSE, (uint64_t)fd, 0, 0, 0, 0); }
static inline int64_t sys_fread(int fd, void* b, uint64_t l)
    { return (int64_t)_sc(SYS_FREAD, (uint64_t)fd, (uint64_t)b, l, 0, 0); }
static inline int     sys_getpid(void)
    { return (int)_sc(SYS_GETPID, 0, 0, 0, 0, 0); }
static inline void    sys_yield(void)
    { _sc(SYS_YIELD, 0, 0, 0, 0, 0); }
static inline void    sys_sleep(uint64_t t)
    { _sc(SYS_SLEEP, t, 0, 0, 0, 0); }
static inline void    sys_shutdown(void)
    { _sc(SYS_SHUTDOWN, 0, 0, 0, 0, 0); }
static inline void    sys_reboot(void)
    { _sc(SYS_REBOOT, 0, 0, 0, 0, 0); }
static inline int64_t sys_exec(const char* name)
    { return (int64_t)_sc(SYS_EXEC, (uint64_t)name, 0, 0, 0, 0); }
static inline int64_t sys_fbinfo(uint64_t* buf)
    { return (int64_t)_sc(SYS_FBINFO, (uint64_t)buf, 0, 0, 0, 0); }
static inline int64_t sys_fbwrite(uint64_t x, uint64_t y, uint64_t w,
                                   uint64_t h, void* pixels)
    { return (int64_t)_sc(SYS_FBWRITE, x, y, w, h, (uint64_t)pixels); }
