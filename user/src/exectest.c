#include "../lib/syscall.h"

static uint64_t ustrlen(const char* s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char* s) { sys_write(1, s, ustrlen(s)); }

int main(void) {
    print("exectest: about to sys_exec(\"hello\")\n");
    int64_t r = sys_exec("hello");
    print("exectest: sys_exec returned, r=");
    /* Minimal decimal print, negative-aware, no libc available. */
    char buf[24]; int i = 0;
    uint64_t v = (r < 0) ? (uint64_t)(-r) : (uint64_t)r;
    if (r < 0) buf[i++] = '-';
    char tmp[24]; int ti = 0;
    if (v == 0) tmp[ti++] = '0';
    while (v > 0) { tmp[ti++] = (char)('0' + (v % 10)); v /= 10; }
    while (ti > 0) buf[i++] = tmp[--ti];
    buf[i] = 0;
    print(buf);
    print("\n");

    print("exectest: calling sys_exec(\"hello\") a SECOND time\n");
    int64_t r2 = sys_exec("hello");
    print("exectest: second sys_exec returned, r2=");
    print((r2 == 0) ? "0 (ok)\n" : "nonzero (check above)\n");

    return 0;
}
