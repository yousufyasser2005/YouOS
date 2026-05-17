#include "../lib/syscall.h"

int main(void) {
    print("cat: opening /greeting.txt\n");
    int fd = sys_open("/greeting.txt", 0);
    if (fd < 0) { print("cat: open failed\n"); return 1; }

    char buf[256];
    int64_t n = sys_fread(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        print("cat: read ");
        char tmp[16]; int i = 15; tmp[i] = 0;
        uint64_t v = (uint64_t)n;
        while (v > 0) { tmp[--i] = '0' + (v % 10); v /= 10; }
        print(&tmp[i]);
        print(" bytes:\n");
        sys_write(1, buf, (uint64_t)n);
    }
    sys_close(fd);
    return 0;
}
