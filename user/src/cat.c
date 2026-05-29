#include "../lib/syscall.h"

static void print(const char* s) {
    uint64_t l = 0;
    while (s[l]) l++;
    sys_write(1, s, l);
}

static void print_num(uint64_t v) {
    char tmp[16]; int i = 15; tmp[i] = 0;
    if (!v) { print("0"); return; }
    while (v > 0) { tmp[--i] = '0' + (v % 10); v /= 10; }
    print(&tmp[i]);
}

int main(void) {
    print("cat: opening /greeting.txt\n");
    int fd = sys_open("/greeting.txt", 0);
    if (fd < 0) { print("cat: open failed\n"); return 1; }
    char buf[256];
    int64_t n = sys_fread(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        print("cat: read ");
        print_num((uint64_t)n);
        print(" bytes:\n");
        sys_write(1, buf, (uint64_t)n);
    }
    sys_close(fd);
    return 0;
}
