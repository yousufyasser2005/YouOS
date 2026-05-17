#include "../lib/syscall.h"

#define LINE_MAX 256

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
        /* Echo the character */
        sys_write(1, &c, 1);
        buf[i++] = c;
    }
    buf[i] = 0;
}

static void cmd_help(void) {
    println("YouOS Shell commands:");
    println("  help           - show this help");
    println("  exec <name>    - run a program from initrd");
    println("  cat <file>     - print a file");
    println("  clear          - clear screen");
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
    while ((n = sys_fread(fd, buf, sizeof(buf)-1)) > 0) {
        sys_write(1, buf, n);
    }
    sys_close(fd);
}

static void cmd_exec(const char* name) {
    /* We can't exec from user space yet — need sys_exec syscall */
    /* For now just print a message */
    print("exec: need kernel support to load: ");
    println(name);
}

static void trim(char* s) {
    /* Remove trailing whitespace */
    int n = ustrlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
}

int main(void) {
    println("YouOS User Shell - type 'help' for commands");

    char line[LINE_MAX];
    while (1) {
        print("$ ");
        readline(line, LINE_MAX);
        trim(line);

        if (line[0] == 0) continue;

        if (ustrcmp(line, "help") == 0) {
            cmd_help();
        } else if (ustrcmp(line, "shutdown") == 0) {
        println("Shutting down...");
        sys_shutdown();
    } else if (ustrcmp(line, "reboot") == 0) {
        println("Rebooting...");
        sys_reboot();
    } else if (ustrcmp(line, "exit") == 0) {
            println("Goodbye!");
            sys_exit(0);
        } else if (ustrcmp(line, "pid") == 0) {
            println("(PID syscall not wired to print yet)");
        } else if (ustrncmp(line, "cat ", 4) == 0) {
            cmd_cat(line + 4);
        } else if (ustrncmp(line, "exec ", 5) == 0) {
            cmd_exec(line + 5);
        } else {
            print("unknown command: ");
            println(line);
        }
    }
    return 0;
}
