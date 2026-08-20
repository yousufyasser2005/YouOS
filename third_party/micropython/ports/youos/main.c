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
#include "py/stream.h"
#include "py/misc.h"

// ---- HAL: the one function that matters -------------------------------
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    sys_write(1, str, len);
}

// ---- import stubs (see sandbox main.c for the reasoning) --------------
mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

typedef struct _youos_file_obj_t {
    mp_obj_base_t base;
    int fd;
} youos_file_obj_t;

static mp_uint_t youos_file_read(mp_obj_t o_in, void *buf, mp_uint_t size, int *errcode) {
    youos_file_obj_t *self = MP_OBJ_TO_PTR(o_in);
    if (self->fd < 0) {
        *errcode = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    int64_t n = sys_fread(self->fd, buf, size);
    if (n < 0) {
        *errcode = MP_EIO;
        return MP_STREAM_ERROR;
    }
    return (mp_uint_t)n;
}

static mp_uint_t youos_file_ioctl(mp_obj_t o_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    (void)arg;
    youos_file_obj_t *self = MP_OBJ_TO_PTR(o_in);
    switch (request) {
        case MP_STREAM_CLOSE:
            if (self->fd >= 0) {
                sys_close(self->fd);
                self->fd = -1;
            }
            return 0;
        default:
            *errcode = MP_EINVAL;
            return MP_STREAM_ERROR;
    }
}

static const mp_rom_map_elem_t youos_file_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&mp_stream___exit___obj) },
};
static MP_DEFINE_CONST_DICT(youos_file_locals_dict, youos_file_locals_dict_table);

static const mp_stream_p_t youos_file_stream_p = {
    .read = youos_file_read,
    .ioctl = youos_file_ioctl,
    .is_text = true,
};

MP_DEFINE_CONST_OBJ_TYPE(
    youos_type_file,
    MP_QSTR_TextIOWrapper,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    protocol, &youos_file_stream_p,
    locals_dict, &youos_file_locals_dict
    );

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)n_args;
    (void)kwargs; // mode/encoding kwargs ignored for now -- read-only
    const char *path = mp_obj_str_get_str(args[0]);
    int fd = sys_open(path, 0);
    if (fd < 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    youos_file_obj_t *o = mp_obj_malloc(youos_file_obj_t, &youos_type_file);
    o->fd = fd;
    return MP_OBJ_FROM_PTR(o);
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

static int mpy_readline(char *buf, int max, const char *prompt, const char *prefill, int prefill_len) {
    sys_write(1, prompt, ustrlen_(prompt));
    int i = 0;
    if (prefill_len > 0) {
        sys_write(1, prefill, prefill_len);
        for (int k = 0; k < prefill_len && i < max - 1; k++) {
            buf[i++] = prefill[k];
        }
    }
    while (i < max - 1) {
        char c;
        // Matches shell.c's readline() exactly: sys_read(0, &c, 1) is
        // trusted to block until a byte arrives, with no return-value
        // check. That's the established, working precedent in this
        // codebase -- don't invent different error-handling here, it
        // was tried in the sandbox and produced a busy-loop failure mode
        // that shell.c's simpler approach doesn't have.
        sys_read(0, &c, 1);
        if (c == 27) { // ESC: abort input, signal caller to exit the REPL
            sys_write(1, "\n", 1);
            return -1;
        }
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

// ---- yourun: run a single .py file non-interactively, then exit -------
// Reached when this process was sys_exec'd with an argument (see main()
// below) -- e.g. desktop.c's "yourun <path>" command. No banner, no REPL:
// matches the semantics of exec'ing a normal program, not starting an
// interactive session.
static void run_script(const char *src, size_t len, const char *name) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(qstr_from_str(name), src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}

static void run_script_file(const char *path) {
    unsigned int fsize = 0;
    unsigned char isdir = 0;
    if (sys_stat(path, &fsize, &isdir) < 0) {
        const char msg[] = "yourun: cannot stat file\n";
        sys_write(1, msg, sizeof(msg) - 1);
        return;
    }
    if (isdir) {
        const char msg[] = "yourun: is a directory\n";
        sys_write(1, msg, sizeof(msg) - 1);
        return;
    }
    int fd = sys_open(path, 0);
    if (fd < 0) {
        const char msg[] = "yourun: cannot open file\n";
        sys_write(1, msg, sizeof(msg) - 1);
        return;
    }
    char *buf = m_new(char, fsize + 1);
    int64_t n = sys_fread(fd, buf, fsize);
    sys_close(fd);
    if (n < 0) {
        n = 0;
    }
    buf[n] = '\0';
    run_script(buf, (size_t)n, path);
}

// Mirrors shared/readline's readline_auto_indent algorithm: carry the
// just-typed line's own indent level forward, plus one more level if it
// ends with ':'. Returns the number of spaces to pre-fill the next line
// with. Sandbox-verified (including nested blocks and interaction with
// manual backspace-to-dedent for else/elif) before this touched the
// real build.
static int compute_next_indent(const char *line) {
    int lead = 0;
    while (line[lead] == ' ') {
        lead++;
    }
    int len = 0;
    while (line[len]) {
        len++;
    }
    int last = len - 1;
    while (last >= 0 && line[last] == ' ') {
        last--;
    }
    int n = lead / 4;
    if (last >= 0 && line[last] == ':') {
        n++;
    }
    return n * 4;
}

static void run_repl(void) {
    char linebuf[512];
    for (;;) {
        int n = mpy_readline(linebuf, sizeof(linebuf), mp_repl_get_ps1(), NULL, 0);
        if (n < 0) {
            return; // ESC pressed -- back to main(), which returns -> crt0 sys_exit()
        }
        if (n == 0) {
            continue;
        }

        vstr_t line;
        vstr_init(&line, 32);
        vstr_add_str(&line, linebuf);

        int aborted = 0;
        int indent = compute_next_indent(linebuf);
        while (mp_repl_continue_with_input(vstr_null_terminated_str(&line))) {
            vstr_add_byte(&line, '\n');
            char pad[64];
            int pn = indent;
            if (pn > (int)sizeof(pad)) {
                pn = sizeof(pad);
            }
            for (int k = 0; k < pn; k++) {
                pad[k] = ' ';
            }
            int n2 = mpy_readline(linebuf, sizeof(linebuf), mp_repl_get_ps2(), pad, pn);
            if (n2 < 0) {
                aborted = 1;
                break;
            }
            // If the user pressed Enter without typing anything beyond
            // the auto-inserted indent, this line is all whitespace --
            // treat it as genuinely blank (matching plain Enter-to-end-
            // block behavior), or mp_repl_continue_with_input never sees
            // a truly empty line and the block never terminates. (Caught
            // in the sandbox: without this, every auto-indented block
            // hung forever waiting for a blank line that could never
            // arrive.)
            int blank_only_indent = 1;
            for (int k = 0; k < n2; k++) {
                if (linebuf[k] != ' ') {
                    blank_only_indent = 0;
                    break;
                }
            }
            if (!blank_only_indent) {
                vstr_add_str(&line, linebuf);
                indent = compute_next_indent(linebuf);
            }
        }
        if (aborted) {
            vstr_clear(&line);
            return; // ESC pressed mid-block -- exit the whole REPL, same as at the prompt
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

    char script_path[256];
    int64_t arglen = sys_get_exec_arg(script_path, sizeof(script_path));

    if (arglen > 0) {
        // yourun mode: run the given script, then exit. sys_exec cleared
        // to a raw full-screen console before jumping here (same one the
        // REPL uses -- not the desktop GUI's own windowed Terminal), and
        // script execution is fast enough that returning immediately
        // makes the whole thing look like a screen flicker with nothing
        // readable. Pause for a keypress so the output actually sticks
        // around long enough to read before handing control back.
        run_script_file(script_path);
        const char donemsg[] = "\n[yourun: finished -- press any key to return]\n";
        sys_write(1, donemsg, sizeof(donemsg) - 1);
        char anykey;
        sys_read(0, &anykey, 1);
    } else {
        const char banner[] = "YouOS MicroPython -- int-only build, no imports yet (ESC to exit)\n";
        sys_write(1, banner, sizeof(banner) - 1);
        run_repl();
    }

    // mp_deinit()/return here is what lets main() reach crt0.asm's
    // post-call sys_exit(rax), handing control back to the shell (or
    // whatever exec'd this) -- reached either after ESC (REPL mode) or
    // right after the script finishes (yourun mode).
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
