#include "../lib/syscall.h"

int main(void) {
    const char msg1[] = "Hello from ELF userspace!\n";
    const char msg2[] = "This is a real ELF binary loaded from initrd!\n";
    sys_write(1, msg1, sizeof(msg1)-1);
    sys_write(1, msg2, sizeof(msg2)-1);
    return 0;
}
