#include "../lib/syscall.h"

int main(void) {
    print("Hello from ELF userspace!\n");
    print("This is a real ELF binary loaded from initrd!\n");
    return 0;
}
