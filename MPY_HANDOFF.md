# YouOS — MicroPython Port, Handoff #2 (real import + GUI-binding feasibility)

**Path:** `/home/yousuf/codes/YouOS` · x86_64, C+NASM, Meson/Ninja (kernel),
Makefile (userspace), GRUB2, QEMU.

This doc hands off from the session that got `mpy` (MicroPython) from
nothing to a working, file-capable, hashable REPL. That work is committed
and pushed; this doc summarizes it briefly and focuses on what's next:
**real `import`**, and an honest feasibility read on pygame/tkinter/PyQt6.

## Conventions (carried over, still apply)

- **No filesystem access to the user's machine.** All edits are
  `python3 - << 'PYEOF' ... PYEOF` heredocs (`assert old in s; s=s.replace(old,new,1);
  open(p,"w").write(s)`) or `cat > path << 'EOF'` for new/rewritten files. The
  user runs commands and pastes back output. Never assume a command
  succeeded without seeing its output.
- **View the real current file before patching, every time.** This bit twice
  this session already (a patch silently skipped because a prior message
  never got run; a whitespace mismatch on a line that looked identical).
  `grep`/`sed -n` the exact current content immediately before writing an
  exact-match patch — don't trust memory of what a file "should" contain.
- **Sandbox-test anything algorithmic before it touches the real build.**
  The single highest-value practice across this whole port. A full host-gcc
  sandbox already exists at `third_party/micropython/ports/youos-sandbox/`
  (gitignored-equivalent — actually it's NOT committed; it's a local
  scratch dir under `third_party/micropython/ports/`, safe to keep using or
  recreate). It builds with plain `gcc`, uses `fopen`/`fgetc`/host stdio in
  place of `sys_open`/`sys_read`, and can be exercised with real `.py`
  files, piped multi-line REPL input, and `-fsanitize=address,undefined`.
  Every feature this session (open(), yourun, auto-indent, hashlib) was
  proven there first, and it caught **real bugs** before they ever reached
  hardware: a busy-loop from a wrong EOF assumption, a runaway `...` prompt
  from auto-indent corrupting blank-line detection, an ASan/conservative-GC
  interaction that looks like a bug but isn't
  (`ASAN_OPTIONS=detect_stack_use_after_return=0` fixes it).
- **`SRC_QSTR` gotcha, worth remembering every time a new file is added to
  `SRC_C`:** `py.mk`'s default `SRC_QSTR` only scans `py/` core files, not
  this port's own sources. Any new file that introduces its own
  `MP_QSTR_*` identifier (a type name, a method name) needs to be listed in
  **both** `SRC_C` and `SRC_QSTR` in
  `third_party/micropython/ports/youos/Makefile`, or the build fails at the
  final compile step with a confusing "undeclared" error instead of
  failing cleanly at qstr-generation time. Bit us twice already
  (`main.c`'s `TextIOWrapper`, `modhashlib.c`'s `sha256`/`update`/`digest`).
- **Build/boot commands** (unchanged from before, still accurate):
```bash
  cd /home/yousuf/codes/YouOS
  python3 tools/mkinitrd.py iso/boot/initrd.img user/bin/hello user/bin/cat user/bin/shell user/bin/fbtest user/bin/desktop user/bin/mpy
  touch kernel/fs/initrd_blob.asm
  cd build && ninja -t clean && ninja 2>&1 | tail -60 && cd ..
  cp build/kernel/youos.elf iso/boot/youos.elf
  grub-mkrescue -o build/youos.iso iso 2>&1 | tail -5

  qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide -cdrom build/youos.iso \
    -m 256M -boot d -serial stdio -display gtk,zoom-to-fit=on \
    -device isa-debug-exit,iobase=0x604,iosize=0x02 \
    -device piix3-usb-uhci -device usb-tablet \
    -netdev user,id=n0 -device rtl8139,netdev=n0,mac=52:54:00:12:34:56 \
    -audiodev pa,id=snd0 -device AC97,audiodev=snd0 -enable-kvm -cpu host
```
  Only rebuild the parts that changed: userspace-only changes skip the
  kernel `ninja` step; kernel-only changes (like the exec-nesting fix)
  skip `mkinitrd`. When in doubt, rebuild everything — it's cheap.
- **The only way to reach `mpy` is desktop → open the Terminal app →
  type `shell` → `exec mpy`, or the desktop terminal's own `yourun <path>`
  command directly.** There is no bare boot-to-shell path on this build.
  This matters for testing: anything you exec is *at least* one level of
  nested `exec` deep (terminal → shell), sometimes two (→ mpy). The
  nested-exec kernel bug (see below) was invisible until this exact path
  was exercised for the first time.

## What's done (7 commits, in order)

1. `4ffc3f4` — **First working `mpy` REPL.** Vendored MicroPython core
   (`py/` + `shared/runtime/`) under `third_party/micropython/`, wrote the
   YouOS port layer (`third_party/micropython/ports/youos/`): freestanding
   libc shim (`libc_shim.c` — memcpy/memset/memmove/memcmp/strlen/strcmp/
   strncmp/strchr/abs, all real, sandbox-tested against libc under ASan/UBSan;
   everything else in the stub headers under `ports/youos/include/` is
   declaration-only dead code, verified via zero-undefined-symbols checks),
   `mpconfigport.h` (int-only: `MICROPY_FLOAT_IMPL_NONE`, no FPU/SSE on
   YouOS at all), REPL loop over `sys_read`/`sys_write` mirroring
   `shell.c`'s `readline()`. Built as a 5th userspace program
   (`bin/mpy`) via `user/Makefile`, same `crt0.o`+`user.ld` pattern as
   `hello`/`cat`/`shell`/`fbtest`/`desktop`.
2. `d606f07` — **Fixed a real, pre-existing kernel bug**: nested
   `sys_exec()` calls shared a single global kernel stack
   (`child_kstack`) and saved-state slot, so a second level of `exec`
   (e.g. desktop → shell → anything) silently corrupted the outer level
   and hung forever on return. Converted to per-nesting-level arrays
   (`exec_kstacks[level]`, `exec_saved_*[level]`), bounded to
   `MAX_EXEC_DEPTH` (8). Reproduced with the completely unmodified
   `hello` binary, confirming it wasn't `mpy`-specific.
3. `bd69c63` — **ESC-to-exit.** `keyboard.c`'s scancode table mapped ESC
   to ASCII 0 (silently undeliverable via `sys_read`); gave it ASCII 27
   like Backspace already gets. `mpy`'s REPL now exits cleanly back to
   the shell. Also restored `extmod/{virtpin.h,modplatform.h}` — an
   earlier git-hygiene trim had deleted `extmod/` entirely, which turned
   out to be a genuine (if small) build dependency, only caught by
   attempting a truly clean rebuild.
4. `9c02f76` — **Real file I/O.** `open()`/`.read()`/`.close()`, a custom
   `TextIOWrapper`-like object using MicroPython's generic stream
   protocol (`py/stream.c`), backed directly by
   `sys_open`/`sys_fread`/`sys_close`. `with` statements work
   (`__enter__`/`__exit__`). No write mode yet.
5. `f1f2fb3` — **`yourun <path>` in the desktop terminal.** Runs a `.py`
   file non-interactively. Needed passing an argument through `sys_exec`,
   which had no mechanism for it — reused the previously-unused `a2`
   parameter, added one new syscall (`SYS_GET_EXEC_ARG`, slot 42) for
   the child to retrieve it, using the same per-level-array pattern as
   the nested-exec fix. `mpy`'s `main()` is now dual-mode: script mode
   (given an argument) vs REPL mode (no argument).
6. `4cc1702` — **REPL auto-indent**, mirroring `shared/readline`'s
   algorithm (carry indent forward, +1 level after `:`). Line-based
   pre-fill rather than porting the whole VT100-based module.
7. `7de687f` — **`hashlib.sha256`**, the doc's originally-planned first
   module-binding milestone. Vendored `extmod/modhashlib.c` +
   `lib/crypto-algorithms/sha256.c` unmodified from upstream.

## Current capabilities, precisely

Works: arithmetic, strings, loops, functions, exceptions, classes (never
explicitly tested but should work — core interpreter, not something we
touched), `for`/`if`/`while`/`def`/`with`, multi-line blocks with
auto-indent, `open()`/`.read()`/`.close()` against real files on
`ycfs/...` paths, `hashlib.sha256(...).digest()`.

Does NOT work / not implemented: **`import` of any kind** (both
`mp_import_stat` and `mp_lexer_new_from_file` in `main.c` are still hard
stubs returning "not found" — this is the next session's job, see below).
Floats (`MICROPY_FLOAT_IMPL_NONE`, deliberate — no FPU/SSE support in the
kernel at all, a much bigger separate project). File writing (`open()` is
read-only — `sys_save_file`/`SYS_SAVEFILE` exists at the kernel level and
could back a write mode, not yet wired up). `os`, `sys` modules
(`MICROPY_PY_OS`/`MICROPY_PY_SYS` both off). Any GUI/window/audio/input
binding (see feasibility section below).

## Next session: real `import`

Two things need real implementations in
`third_party/micropython/ports/youos/main.c`, replacing the current
stubs:

**`mp_import_stat(const char *path)`** — needs to actually check whether
`path` exists and whether it's a file or directory. `sys_stat` already
exists and already works (used by `run_script_file`) — this is mostly a
matter of calling it and mapping the result to
`MP_IMPORT_STAT_FILE`/`MP_IMPORT_STAT_DIR`/`MP_IMPORT_STAT_NO_EXIST`.

**`mp_lexer_new_from_file(qstr filename)`** — needs to actually open,
read, and lex the file. `run_script_file` in the current `main.c` already
does almost exactly this (stat for size, `sys_open`, `sys_fread` into an
`m_new`-allocated buffer) — the logic is 90% reusable, just needs to
return a `mp_lexer_t*` via `mp_lexer_new_from_str_len` instead of directly
executing.

**The real design question is module search path.** MicroPython's
`builtinimport.c` walks `sys.path` to find modules — but
`MICROPY_PY_SYS` is off (no `sys` module registered at all right now).
Options, roughly in order of effort:
1. **Hardcode a single fixed search location** (e.g. always look in
   `ycfs/lib/<modname>.py`, ignoring any notion of "current directory" or
   multiple search paths). Simplest, no `sys` module needed, but
   inflexible — `import mymodule` from a script in `ycfs/projects/foo/`
   wouldn't find `ycfs/projects/foo/helper.py` sitting right next to it.
2. **Search relative to the running script's own directory, plus one
   fixed fallback** (e.g. `ycfs/lib/`). Better UX, still no `sys` module
   needed — `run_script_file` already knows the script's path, so the
   directory is one string-truncation away.
3. **Turn on a minimal `sys.path`** (`MICROPY_PY_SYS = 1`, populate
   `mp_sys_path` with one or two hardcoded entries). Most "real Python"-
   feeling, but pulls in more of `modsys.c` than we've needed so far —
   worth checking what else that drags in before committing to it.

My instinct is **option 2** — it's the best UX-to-effort ratio and
doesn't require deciding what a YouOS `sys.path` even means yet. But this
is worth a real discussion at the start of next session rather than me
just picking one silently.

**Also worth deciding:** does a first version need package support
(`import mypackage` where `mypackage/` is a directory with
`__init__.py`), or is single-file-module import (`import mymodule` →
`mymodule.py`) enough for a first pass? I'd suggest single-file only
first — packages add real complexity (directory walking, `__init__.py`
execution order) for a feature nobody's asked for yet.

**Testing plan, same discipline as everything else:** write a test
`.py` file with a corresponding "module" `.py` file next to it in the
sandbox, get `import`+attribute-access+function-call all working and
byte-matched against real CPython's behavior *in the sandbox* first
(the sandbox's stub `mp_import_stat`/`mp_lexer_new_from_file` need real
implementations too, backed by `fopen` instead of `sys_open` — same
dual-implementation pattern as every other feature this session), before
touching `third_party/micropython/ports/youos/main.c` for real.

## pygame / tkinter / PyQt6 — honest feasibility read

Same conclusion the original handoff doc reached, now with much more
concrete grounding since `mpy` is a real, working userspace program with
proven file I/O and a proven module-binding mechanism (`hashlib`):

- **PyQt6: not achievable, full stop.** Needs real Qt — a massive C++ GUI
  framework with its own event loop, widget toolkit, and platform
  abstraction layer. Porting it would be a multi-month project on its
  own, independent of MicroPython entirely.
- **tkinter: not achievable as literal tkinter.** Needs Tcl/Tk. Same
  category of problem as Qt, smaller but still a real windowing toolkit
  YouOS has no equivalent of.
- **pygame: not achievable as literal pygame** (needs SDL, which YouOS
  doesn't have) **but a custom, pygame-*inspired* module binding directly
  to YouOS's own real primitives is genuinely realistic** — this is
  the most promising of the three by a wide margin, and it's exactly the
  path the original handoff doc scoped out. What YouOS actually has,
  confirmed working via boot log and existing syscalls (not yet explored
  in detail — this is next session's discovery work, same kind of
  `grep`/`sed` investigation as `keyboard.c`/`syscall.c` this session):
  - **Framebuffer**: `sys_fbinfo`/`sys_fbwrite` syscalls exist and work
    (`fbtest` program, `sys_exec`'s own `fb_fill`/`fb_terminal_init`
    calls). A `Surface`-like object wrapping `sys_fbwrite` is very
    plausible.
  - **Window manager primitives**: the *first* handoff doc (this port's
    original planning doc, before any of this session's work) explicitly
    named `wm_new`, `px`, `text`, `rect` as YouOS's own window-manager
    functions, intended as the eventual binding target. **These have
    never actually been located/read this session** — first thing to do
    next time this is picked up: `grep -rn "wm_new\|^void px\|^void text\|^void rect"
    kernel/ include/` to find where they actually live and what their
    real signatures are.
  - **Audio**: `sys_play_pcm`/`sys_play_stream`/`sys_pcm_done` exist and
    are proven working (boot log: "AC97 self-test: playback interrupt
    fired"). A `pygame.mixer`-inspired `sound.play(samples)` binding is
    very plausible.
  - **Input**: `sys_keypoll` (non-blocking keyboard) and `sys_mouseread`
    both exist and work (used by `desktop.c` already). A
    `pygame.event`-inspired polling loop binding is plausible.
  - Naming: call it something that is **not** `pygame` (e.g. `yougame`,
    matching the `youdo`/`yourun`/YCFS naming convention already
    established in this codebase) — the original doc was explicit about
    this, and it still applies: "these would be inspired by, not literal
    reimplementations of, pygame/tkinter APIs... say this explicitly in
    any design doc/comments so nobody downstream thinks real pygame is
    running."
  - **Scope for a first pass, if this is picked up**: probably a `Surface`
    (framebuffer-backed drawing: pixel/rect/text, matching what `px`/
    `text`/`rect` already give the kernel side), a basic event-polling
    loop (keyboard + mouse), and *maybe* simple sound playback — not a
    full pygame-shaped API surface, a small binding proven end-to-end
    the same way `hashlib` was.

## Summary for whoever picks this up next

Real `import` is genuinely the right next step — it's what makes `mpy`
a scripting environment instead of a REPL toy, and it's a bounded,
well-understood piece of work (two stub functions, one design decision
about search-path shape). The pygame-inspired binding is real and
worth pursuing, but it needs a discovery pass on `wm_new`/`px`/`text`/
`rect` before any design work can start — don't skip that step.
