// mpconfigport.h — YouOS userspace port (real freestanding build)
//
// This is the REAL port, built with x86_64-elf-gcc -ffreestanding -nostdlib,
// linked as a fifth userspace program alongside hello/cat/shell/fbtest/desktop
// via user/Makefile + crt0.asm + user.ld. Every decision here was proven
// first in the host-gcc sandbox (ports/youos-sandbox) — see handoff doc.

#include <stdint.h>

// CORE_FEATURES, not MINIMUM: sandbox testing showed MINIMUM silently
// disables slicing (data[1:]), which is a normal thing for anyone typing
// at a REPL to reach for.
#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)

// Constraint #1 (handoff doc): no FPU/SSE on YouOS (userspace build flags
// are -mno-mmx -mno-sse -mno-sse2, and the kernel has no FPU context-switch
// support). Int-only, not a temporary shortcut -- float support is a
// separate later project.
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_NONE)

#define MICROPY_ERROR_REPORTING                 (MICROPY_ERROR_REPORTING_NORMAL)

// No <errno.h> on this bare cross-compiler (no libc). MicroPython has its
// own self-contained MP_Exxx codes for exactly this situation -- avoids
// stubbing yet another system header for something with a real config
// option already.
#define MICROPY_USE_INTERNAL_ERRNO              (1)

// No real OS-shaped sys/io/os module content to back these with yet.
#define MICROPY_PY_SYS                          (0)
#define MICROPY_PY_IO                           (0)
#define MICROPY_PY_OS                           (0)

// We provide our own REPL I/O loop by hand in main.c (mirroring shell.c's
// readline over sys_read), but DO use py/repl.c's mp_repl_continue_with_input
// for multi-line block detection (if/for/def) -- it's pure parsing logic,
// no I/O assumptions, so it's safe to force on even under CORE_FEATURES.
#define MICROPY_PY_BUILTINS_INPUT               (0)
#define MICROPY_HELPER_REPL                     (1)
#define MICROPY_PY_SYS_PS1_PS2                  (0)

// No filesystem-backed import yet (no VFS/POSIX reader wired) -- see
// mp_import_stat/mp_builtin_open/mp_lexer_new_from_file stubs in main.c.
// `import` of on-disk .py files is a deliberate later step, same as the
// sandbox.

typedef long mp_off_t;
typedef long mp_int_t;
typedef unsigned long mp_uint_t;

// No <alloca.h> under -ffreestanding/-nostdlib; py/ core only needs
// alloca() when MICROPY_PY_BUILTINS_INPUT or a few other optional features
// are on, which are off here. If a future feature pulls it in, provide a
// GCC-builtin-based one here rather than assuming libc.
#define alloca(x) __builtin_alloca(x)

#define MICROPY_HW_BOARD_NAME "YouOS"
#define MICROPY_HW_MCU_NAME   "x86_64"
