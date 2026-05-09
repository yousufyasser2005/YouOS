#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/*
 * Syscall numbers — must match kernel/syscall.h
 */
#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_GETPID  3
#define SYS_YIELD   4
#define SYS_SLEEP   5
#define SYS_SBRK    6

/*
 * Raw syscall — inline assembly
 */
static inline uint64_t syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t syscall1(uint64_t num, uint64_t a1) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(num), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t syscall3(uint64_t num,
                                 uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/*
 * Userspace C library functions
 */
static inline void exit(int code) {
    syscall1(SYS_EXIT, (uint64_t)code);
    while (1);
}

static inline int write(int fd, const void* buf, size_t len) {
    return (int)syscall3(SYS_WRITE, (uint64_t)fd,
                         (uint64_t)buf, (uint64_t)len);
}

static inline int getpid(void) {
    return (int)syscall0(SYS_GETPID);
}

static inline void yield(void) {
    syscall0(SYS_YIELD);
}

static inline void puts_user(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

#endif /* USER_SYSCALL_H */
