/* TEMPORARY (but kept permanently, like crashtest.c/exectest.c/
 * spawntest.c) -- regression check for Phase 2 of true concurrent
 * multi-program execution (per-process I/O redirection). Writes a
 * known greeting, reads one line, then echoes back what it read --
 * proving both directions of the redirection (sys_write() -> this
 * process's own output queue, sys_read() <- this process's own input
 * queue) work correctly when spawned windowed, without any real
 * window manager involved yet. The caller (e.g. a temporary desktop.c
 * test command, or any other harness) is expected to:
 *   1. sys_spawn_windowed("iotest")
 *   2. sys_msgrecv() on "term_out_<pid>" to see the greeting
 *   3. sys_msgpost() a line onto "term_in_<pid>" (one message per
 *      character, ending in '\n' -- the convention sys_read()'s
 *      windowed path expects, matching what a real window manager
 *      would post per keystroke in Phase 3)
 *   4. sys_msgrecv() again to see it echoed back
 * This program itself needs no changes to do any of that -- it just
 * calls sys_write()/sys_read() exactly like shell.c does. */
#include "../lib/syscall.h"

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }

int main(void) {
    const char* greeting = "iotest: hello from windowed I/O\n";
    sys_write(1, greeting, (uint64_t)slen(greeting));

    char line[64];
    uint64_t n = sys_read(0, line, sizeof(line) - 1);
    if ((int64_t)n < 0) n = 0;

    const char* prefix = "iotest: echo: ";
    sys_write(1, prefix, (uint64_t)slen(prefix));
    sys_write(1, line, n);
    if (n == 0 || line[n - 1] != '\n') sys_write(1, "\n", 1);

    return 0;
}
