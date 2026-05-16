#include "../lib/syscall.h"

/* Additional syscalls */
#define SYS_OPEN   6
#define SYS_CLOSE  7
#define SYS_FREAD  8

static inline int64_t sys_open(const char* path, int flags) {
    uint64_t ret;
    __asm__ volatile("syscall"
        : "=a"(ret) : "0"((uint64_t)SYS_OPEN), "D"((uint64_t)path), "S"((uint64_t)flags)
        : "rcx", "r11", "memory");
    return (int64_t)ret;
}
static inline int sys_close(int fd) {
    uint64_t ret;
    __asm__ volatile("syscall"
        : "=a"(ret) : "0"((uint64_t)SYS_CLOSE), "D"((uint64_t)fd)
        : "rcx", "r11", "memory");
    return (int)ret;
}
static inline int64_t sys_fread(int fd, void* buf, uint64_t size) {
    uint64_t ret;
    __asm__ volatile("syscall"
        : "=a"(ret) : "0"((uint64_t)SYS_FREAD), "D"((uint64_t)fd), "S"((uint64_t)buf), "d"(size)
        : "rcx", "r11", "memory");
    return (int64_t)ret;
}

int main(void) {
    print("cat: opening /hello\n");
    int fd = sys_open("/hello", 0);
    if (fd < 0) { print("cat: open failed\n"); return 1; }

    char buf[256];
    int64_t n = sys_fread(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        print("cat: read ");
        /* print n as decimal */
        char tmp[16]; int i = 15; tmp[i] = 0;
        uint64_t v = (uint64_t)n;
        while (v > 0) { tmp[--i] = '0' + (v % 10); v /= 10; }
        print(&tmp[i]);
        print(" bytes:\n");
        sys_write(buf, (uint64_t)n);
    }
    sys_close(fd);
    return 0;
}
