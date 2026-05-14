#include <stdint.h>

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_GETPID  3

static inline uint64_t syscall1(uint64_t num, uint64_t a1) {
    uint64_t ret;
    __asm__ volatile ("syscall"
        : "=a"(ret) : "0"(num), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall3(uint64_t num,
                                  uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    __asm__ volatile ("syscall"
        : "=a"(ret) : "0"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ volatile ("syscall"
        : "=a"(ret) : "0"(num) : "rcx", "r11", "memory");
    return ret;
}

static void print(const char* s) {
    uint64_t len = 0;
    while (s[len]) len++;
    syscall3(SYS_WRITE, 1, (uint64_t)s, len);
}

void hello_main(void)
{
    print("Hello from Ring 3 userspace!\n");
    print("Syscalls work from Ring 3!\n");
    uint64_t pid = syscall0(SYS_GETPID);
    char msg[] = "PID: X\n";
    msg[5] = '0' + (char)(pid % 10);
    print(msg);
    print("Exiting...\n");
    syscall1(SYS_EXIT, 0);
    while(1);
}
