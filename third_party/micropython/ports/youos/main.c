// main.c — YouOS userspace MicroPython REPL.
//
// Built as a fifth userspace program alongside hello/cat/shell/fbtest/desktop
// (see user/Makefile). The REPL loop logic here (accumulate a line, ask
// mp_repl_continue_with_input whether the statement is complete, keep
// reading with a "..." prompt if not, then compile+execute) is identical
// to ports/youos-sandbox/main.c's run_repl(), which was verified
// byte-for-byte against real CPython's REPL output before this was
// written. Only the character source/sink changed: sys_read/sys_write
// instead of host stdin/stdout.
//
// No argc/argv (crt0.asm's _start calls main() with none), no file-backed
// import (mp_import_stat/mp_builtin_open/mp_lexer_new_from_file below all
// report "doesn't exist" -- same as the sandbox, deliberately out of scope
// for this milestone).

#include "syscall.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/mperrno.h"
#include "py/repl.h"
#include "py/builtin.h"
#include "py/lexer.h"

// ---- HAL: the one function that matters -------------------------------
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    sys_write(1, str, len);
}

// ---- import stubs (see sandbox main.c for the reasoning) --------------
mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)n_args;
    (void)args;
    (void)kwargs;
    mp_raise_OSError(MP_ENOENT);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    (void)filename;
    mp_raise_OSError(MP_ENOENT);
}

// ---- heap ---------------------------------------------------------------
// Static array baked into this binary. No kernel-heap syscall exists for
// userspace (confirmed: kmalloc/kfree are kernel-only, no SYS_SBRK in the
// live user/lib/syscall.h) -- this mirrors what the sandbox already
// proved works (gc_init over a static buffer, real reclamation verified
// under allocation pressure).
//
// NOTE: size chosen to match the sandbox's proven-good 1MB. If YouOS's
// user.ld / process memory layout can't fit this, shrink it -- but if
// you do, rerun the gc pressure test pattern from the handoff doc's
// Milestone 2 against the new size before trusting it.
#define YOUOS_MPY_HEAP_SIZE (1024 * 1024)
static char mpy_heap[YOUOS_MPY_HEAP_SIZE];

// ---- REPL: identical loop logic to ports/youos-sandbox/main.c ---------

// crt0.asm gives us no libc, so a tiny local strlen -- distinct name from
// the libc_shim.c's strlen so there's no ambiguity about which one this is.
static uint64_t ustrlen_(const char *s) {
    uint64_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static int mpy_readline(char *buf, int max, const char *prompt) {
    sys_write(1, prompt, ustrlen_(prompt));
    int i = 0;
    while (i < max - 1) {
        char c;
        // Matches shell.c's readline() exactly: sys_read(0, &c, 1) is
        // trusted to block until a byte arrives, with no return-value
        // check. That's the established, working precedent in this
        // codebase -- don't invent different error-handling here, it
        // was tried in the sandbox and produced a busy-loop failure mode
        // that shell.c's simpler approach doesn't have.
        sys_read(0, &c, 1);
        if (c == '\n' || c == '\r') {
            sys_write(1, "\n", 1);
            break;
        }
        if (c == '\b' || c == 127) {
            if (i > 0) {
                i--;
                sys_write(1, "\b \b", 3); // erase char on screen, same as shell.c's backspace
            }
            continue;
        }
        sys_write(1, &c, 1); // echo
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

static void run_repl(void) {
    char linebuf[512];
    for (;;) {
        int n = mpy_readline(linebuf, sizeof(linebuf), mp_repl_get_ps1());
        if (n == 0) {
            continue;
        }

        vstr_t line;
        vstr_init(&line, 32);
        vstr_add_str(&line, linebuf);

        while (mp_repl_continue_with_input(vstr_null_terminated_str(&line))) {
            vstr_add_byte(&line, '\n');
            mpy_readline(linebuf, sizeof(linebuf), mp_repl_get_ps2());
            vstr_add_str(&line, linebuf);
        }

        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_,
                vstr_str(&line), vstr_len(&line), 0);
            qstr source_name = lex->source_name;
            mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_SINGLE_INPUT);
            mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
            mp_call_function_0(module_fun);
            nlr_pop();
        } else {
            mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        }
        vstr_clear(&line);
    }
}

int main(void) {
    mp_stack_ctrl_init();
    gc_init(mpy_heap, mpy_heap + YOUOS_MPY_HEAP_SIZE);
    mp_init();

    const char banner[] = "YouOS MicroPython -- int-only build, no imports yet\n";
    sys_write(1, banner, sizeof(banner) - 1);
    run_repl();

    // Not reachable in normal operation (run_repl() never returns), but
    // keep mp_deinit()/return here for symmetry with the sandbox in case
    // a future exit command is added.
    mp_deinit();
    return 0;
}

// Called if an exception escapes all C exception-catching handlers.
// Nothing sensible to do without a libc abort()/exit() beyond crt0's own
// exit path -- loop forever rather than falling into undefined behavior,
// mirroring bare-arm's nlr_jump_fail().
void nlr_jump_fail(void *val) {
    (void)val;
    const char msg[] = "\n*** YouOS MicroPython: FATAL uncaught NLR jump ***\n";
    sys_write(1, msg, sizeof(msg) - 1);
    for (;;) {
    }
}
