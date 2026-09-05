#include "../lib/syscall.h"

#define LINE_MAX 256

static uint64_t ustrlen(const char* s) {
    uint64_t n = 0; while (s[n]) n++; return n;
}
static int ustrcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int ustrncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
static void print(const char* s) { sys_write(1, s, ustrlen(s)); }
static void println(const char* s) { print(s); print("\n"); }
static void print_dec(uint64_t n) {
    if (n == 0) { print("0"); return; }
    char buf[21]; int i = 20; buf[i] = 0;
    while (n > 0) { buf[--i] = (char)('0' + (n % 10)); n /= 10; }
    print(&buf[i]);
}

static void readline(char* buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c;
        sys_read(0, &c, 1);
        if (c == '\n' || c == '\r') { print("\n"); break; }
        if (c == '\b' || c == 127) {
            if (i > 0) { i--; sys_write(1, "\b", 1); }
            continue;
        }
        sys_write(1, &c, 1);
        buf[i++] = c;
    }
    buf[i] = 0;
}

static void trim(char* s) {
    int n = (int)ustrlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
}

static void cmd_help(void) {
    println("YouOS Shell commands:");
    println("  help           - show this help");
    println("  exec <name>    - run a program from initrd");
    println("  cat <file>     - print a file");
    println("  pid            - show current PID");
    println("  ticks          - show scheduler tick count (Phase 2 test)");
    println("  spin           - pure ring-3 busy loop, prints tick delta (Phase 2 test)");
    println("  exit           - exit shell");
    println("  shutdown       - power off");
    println("  reboot         - reboot system");
}

/* Pure ring-3 busy loop -- deliberately makes NO syscalls inside the
 * loop itself, so nothing here can incidentally re-enable interrupts
 * (unlike keyboard_getchar()'s poll loop, which calls process_yield()
 * -> sti on every iteration). This is the real Phase 2 test: if the
 * timer can preempt genuinely-running ring-3 code with no syscalls
 * involved, ticks should advance close to normal (100/sec) during the
 * loop. If it can't (today's IF=0 behavior), ticks should barely move
 * at all regardless of how long the loop runs, since the only ticks
 * counted are from the sys_ticks() calls themselves and whatever ran
 * before/after in kernel context. */
static void cmd_spin(void) {
    uint64_t t0 = sys_ticks();
    print("spin: t0="); print_dec(t0); print("\n");
    volatile uint64_t counter = 0;
    for (volatile uint64_t i = 0; i < 2000000000ULL; i++) {
        counter++;
    }
    uint64_t t1 = sys_ticks();
    print("spin: t1="); print_dec(t1);
    print("  delta="); print_dec(t1 - t0);
    print("  counter="); print_dec((uint64_t)counter); print("\n");
}

static void cmd_cat(const char* path) {
    int fd = sys_open(path, 0);
    if (fd < 0) { print("cat: not found: "); println(path); return; }
    char buf[512];
    int64_t n;
    while ((n = sys_fread(fd, buf, sizeof(buf)-1)) > 0)
        sys_write(1, buf, (uint64_t)n);
    sys_close(fd);
}

int main(void) {
    println("YouOS User Shell - type 'help' for commands");
    char line[LINE_MAX];
    while (1) {
        print("$ ");
        readline(line, LINE_MAX);
        trim(line);
        if (line[0] == 0) continue;
        if      (ustrcmp(line, "help") == 0)     cmd_help();
        else if (ustrcmp(line, "shutdown") == 0) { println("Shutting down..."); sys_shutdown(); }
        else if (ustrcmp(line, "reboot") == 0)   { println("Rebooting..."); sys_reboot(); }
        else if (ustrcmp(line, "exit") == 0)     { println("Goodbye!"); sys_exit(0); }
        else if (ustrcmp(line, "pid") == 0)      { println("(PID syscall not wired to print yet)"); }
        else if (ustrcmp(line, "ticks") == 0)    { print("ticks="); print_dec(sys_ticks()); print("\n"); }
        else if (ustrcmp(line, "spin") == 0)     cmd_spin();
        else if (ustrncmp(line, "cat ", 4) == 0) cmd_cat(line + 4);
        else if (ustrncmp(line, "exec ", 5) == 0) {
            int64_t r = sys_exec(line + 5);
            if (r < 0) { print("exec: not found: "); println(line + 5); }
        } else {
            print("unknown command: "); println(line);
        }
    }
    return 0;
}
