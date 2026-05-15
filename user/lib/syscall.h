#pragma once
#include <stdint.h>

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_GETPID 3

static inline uint64_t syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(num) : "rcx","r11","memory");
    return ret;
}

static inline uint64_t syscall3(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(num),"D"(a1),"S"(a2),"d"(a3) : "rcx","r11","memory");
    return ret;
}

static inline void sys_exit(uint64_t code) {
    syscall0(SYS_EXIT | (code << 32));
}

static inline void sys_write(const char* s, uint64_t len) {
    syscall3(SYS_WRITE, 1, (uint64_t)s, len);
}

static inline void print(const char* s) {
    uint64_t len = 0;
    while (s[len]) len++;
    sys_write(s, len);
}
