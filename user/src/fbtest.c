#include "../lib/syscall.h"

#define WIDTH  300
#define HEIGHT 200

static unsigned int pixels[WIDTH * HEIGHT];

static void print(const char* s) {
    uint64_t l = 0; while (s[l]) l++;
    sys_write(1, s, l);
}

static void print_num(uint64_t v) {
    char tmp[20]; int i = 19; tmp[i] = 0;
    if (!v) { print("0"); return; }
    while (v) { tmp[--i] = '0' + (v % 10); v /= 10; }
    print(&tmp[i]);
}

int main(void) {
    uint64_t info[5];
    if (sys_fbinfo(info) != 0) { print("fbinfo failed\n"); return 1; }

    print("addr="); print_num(info[0]); print("\n");
    print("w=");    print_num(info[1]); print("\n");
    print("h=");    print_num(info[2]); print("\n");
    print("pitch=");print_num(info[3]); print("\n");
    print("bpp=");  print_num(info[4]); print("\n");

    /* Fill entire screen red as a test */
    for (int i = 0; i < WIDTH * HEIGHT; i++)
        pixels[i] = 0xFF0000;

    /* Draw at top-left corner (0,0) so it's definitely visible */
    int64_t r = sys_fbwrite(0, 0, WIDTH, HEIGHT, pixels);
    print("fbwrite result="); print_num((uint64_t)r); print("\n");

    print("Press Enter...\n");
    char c; sys_read(0, &c, 1);
    return 0;
}
