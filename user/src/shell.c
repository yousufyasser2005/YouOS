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
    println("  exit           - exit shell");
    println("  shutdown       - power off");
    println("  reboot         - reboot system");
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
