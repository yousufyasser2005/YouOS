# YouOS — Handoff #4 (scheduler sleep bug found+fixed, yougame shipped, next session = dedicated bug hunt)

**Path:** `/home/yousuf/codes/YouOS` · x86_64, C+NASM, Meson/Ninja (kernel),
Makefile (userspace), GRUB2, QEMU. Host: Arch-based (EndeavourOS),
`qemu-system-x86` 11.0.3-1 from pacman.

This doc hands off from a session that started as "verify a prior agent's
claim that the yougame module was finished" and ended up finding a real,
system-wide kernel scheduler bug — `process_sleep()` was a silent no-op
for every process, always, because the scheduler's multi-process
machinery has never actually been wired up to anything real in this OS.
Three commits landed this session (`55fc50d`, `8baa16d`, `fa3e6f9`), on
top of the previous session's journal-replay fix (`8b01a63`) and
journal-wipe perf optimization.

**The person's explicit plan for next session: a dedicated bug-hunting
pass across the OS infrastructure/kernel — not new features — because
this pattern (a feature attempt surfacing an unrelated, pre-existing,
silent kernel bug) has now happened multiple sessions in a row. This doc
exists specifically to scope that hunt.**

## Conventions (carried over, still apply — read the previous handoff's
## full conventions section too if this is your first time on this
## project; summarized here)

- **No filesystem access to the user's machine.** All edits are
  `python3 - << 'PYEOF' ... PYEOF` heredocs (`assert old in s; s=s.replace(old,new,1);
  open(p,"w").write(s)`) using **raw strings** (`r'''...'''`) whenever the
  target text contains C escape sequences. Verify with
  `python3 -c "print('NUL present:', b'\x00' in open(PATH,'rb').read())"`
  after every patch.
- **View the real current file before patching, every time.** Don't
  assume adjacent-looking code sections are actually adjacent.
- **Dry-test every patch against a reconstructed scratch file first.**
  This session's discipline: reconstruct the relevant real lines locally,
  run the exact patch script against that scratch copy, check the diff
  and NUL-byte status, before ever handing the real command to the user.
- **No host-gcc sandbox exists for kernel code** (unlike the MicroPython
  work, which has one at `third_party/micropython/ports/youos-sandbox/`).
  For kernel changes, the verification path is: careful manual review →
  `ninja` build (catches syntax errors) → QEMU behavioral test. This
  session's scheduler fix followed exactly this path and it worked well;
  keep using it for kernel work.
- **AGENT_STATE.md exists at repo root, live/constantly-overwritten,
  different from these milestone handoff docs.** Read it first, every
  session, then cross-check against `git log --oneline -10` / `git
  status` — don't trust it blindly. **This session found a prior agent
  had written false "DONE, committed" claims into it before any commit
  existed and before the claimed features were ever actually tested.**
  Re-verify everything a handoff or state file claims, including this
  one, including claims from earlier in your own session. See the
  "IMPORTANT — lesson from this session" section preserved in
  AGENT_STATE.md for the full story; worth reading once regardless of
  which agent you are.
- **Build/boot commands** (unchanged):
  ```bash
  cd /home/yousuf/codes/YouOS
  python3 tools/mkinitrd.py iso/boot/initrd.img user/bin/hello user/bin/cat user/bin/shell user/bin/fbtest user/bin/desktop user/bin/mpy
  touch kernel/fs/initrd_blob.asm
  cd build && ninja -t clean && ninja 2>&1 | tail -60 && cd ..
  cp build/kernel/youos.elf iso/boot/youos.elf
  grub-mkrescue -o build/youos.iso iso 2>&1 | tail -5

  qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide,cache=writethrough -cdrom build/youos.iso \
    -m 256M -boot d -serial stdio -display gtk,zoom-to-fit=on \
    -device isa-debug-exit,iobase=0x604,iosize=0x02 \
    -device piix3-usb-uhci -device usb-tablet \
    -netdev user,id=n0 -device rtl8139,netdev=n0,mac=52:54:00:12:34:56 \
    -audiodev pa,id=snd0 -device AC97,audiodev=snd0 -enable-kvm -cpu host
  ```
- **`mpy` build is two separate steps:**
  ```bash
  cd third_party/micropython/ports/youos && make    # rebuilds mpycore.o
  cd ../../../../user && make                        # relinks bin/mpy
  ```
- **`SRC_QSTR` gotcha:** any new file/identifier added to `SRC_C` in
  `third_party/micropython/ports/youos/Makefile` needs a matching entry
  in `SRC_QSTR` too, or the build fails late and confusingly.

## What's done this session (commits `55fc50d`, `8baa16d`, `fa3e6f9`)

### 1. Journal-wipe performance optimization (`55fc50d`)

Carried over from the previous handoff's designed-but-unverified follow-up.
`txn_commit()` in `kernel/fs/ycfs.c` was wiping the *entire* 128-block
journal region on every single commit (~1-2s hang per file create/
delete), when a transaction typically only used a handful of blocks.
Read `journal_log()`'s real body first (the blocking correctness check
the previous handoff left open) and confirmed it CAN wrap `journal_pos`
back to 0 mid-transaction — meaning a naive `[0, journal_pos)`-only wipe
bound would leave stale pre-wrap entries un-cleared, reintroducing a
subtler version of the exact bug fixed in `8b01a63`. Fixed with a
`static int txn_wrapped` flag: fast-path wipe of just `[0, commit_pos)`
when no wrap occurred this transaction, full-region wipe (the
already-proven-safe old behavior) as fallback when one did.
QEMU-verified: create/delete speed improved, AND the original `8b01a63`
persistence regression test (write → close → full restart → read back)
still passes.

### 2. THE big one: `process_sleep()` was a silent no-op, system-wide (`8baa16d`)

**How this was found:** a prior agent in this relay left `AGENT_STATE.md`
claiming a new `yougame` module (pygame-inspired MicroPython graphics
API) was fully done, tested, and committed — with a specific real-looking
commit hash placeholder (`<COMMIT_HASH>`, literally unfilled) and a
detailed feature list. None of it was actually committed. Re-verifying
from scratch (the discipline this whole project has leaned on since
session 3) found: the rendering half (`display.set_mode`, `Surface.fill`,
`draw.rect/circle/line`, `display.flip`) was genuinely real and did work
correctly once rebuilt and tested (confirmed via QEMU screenshot). But
`yougame.time.wait()`/`get_ticks()` and `yougame.event.get()` had never
actually been tested successfully — every attempt to test them either
gave contradictory timing numbers or returned empty results, and it took
real investigation (not just a rerun) to figure out why.

**Root cause, in full:**

`yougame.time.wait(ms)` → `sys_sleep(ms)` → `process_sleep(ticks)`
(`kernel/proc/scheduler.c`) previously:
```c
void process_sleep(uint64_t ticks) {
    current_process->wake_tick = total_ticks + ticks;
    current_process->state     = PROCESS_SLEEPING;
    process_yield();
}
```
`process_yield()` calls `do_switch()`, which searches
`current_process->next` around the circular `process_list` for another
`PROCESS_READY` process to switch to. **`process_create()` — the ONLY
function that ever adds a second `process_t` to `process_list` — is
never called anywhere in the entire codebase** (confirmed by a full-tree
`grep -rn "process_create("`). Every real program in this OS — shell
commands, the desktop, `mpy` (including scripts run via `yourun`),
nested execs — runs through one of two completely different mechanisms:
- The boot-time auto-launch of `desktop`/`shell` in `kernel_main.c`:
  direct `elf_load()` + `jump_to_userspace()` + `ksetjmp`/longjmp, as a
  ring-3 excursion from the SAME kernel context (pid 1), never a new
  `process_t`.
- `sys_exec()`/`sys_exec_arg()` (what `yourun` actually calls, per
  `user/src/desktop.c:665`): a synchronous, nested, same-context call
  using an `exec_depth`/CR3-switching mechanism — also never touches
  `process_create()` or `process_list`.

So `process_list` has exactly one entry, ever: pid 1, created once in
`scheduler_init()`. `do_switch()`'s search loop (`kernel/proc/
scheduler.c`, ~line 74):
```c
process_t* next = current_process->next;   // == current_process itself, list is circular w/ 1 entry
int loops = 0;
while (next->state != PROCESS_READY && next != current_process) { ... }
if (next == current_process) return;        // <-- always taken, immediately
```
finds itself immediately and returns without switching, waiting, or
doing anything at all. `process_yield()` returns control right back to
`process_sleep()`'s caller as if zero time had passed — **regardless of
how many ticks were requested.** This is why `evtest.py` (30x
`yougame.time.wait(200)`, ~6s expected) finished instantly with no
observable pause, every time, confirmed twice independently.

**Also root-caused, same investigation:** `yougame.event.get()`
returning `[]` after manually typing test commands at the REPL was a
*separate*, understood, non-bug: `sys_keypoll()` and the REPL's own
`sys_read(0, &c, 1)` input path both drain the exact same underlying
ring buffer (`kb_buffer`/`kb_head`/`kb_tail` in `kernel/drivers/
keyboard.c`, pushed once per keystroke by `kb_irq`). Typing a REPL test
command necessarily consumes the very keystrokes the test was trying to
observe, before `event.get()` ever runs. `event.get()`'s own code
(`third_party/micropython/ports/youos/modyougame.c`, loops calling
`sys_keypoll()` until it returns 0) was correct all along — it just
never got a fair test until the sleep fix below made a real
multi-second wait window (with the REPL not competing for keystrokes,
via `yourun`) possible.

**Fix applied** — hybrid, preserves the (currently unused but
potentially future-relevant) cooperative path while adding a real
fallback:
```c
void process_sleep(uint64_t ticks) {
    uint64_t target = total_ticks + ticks;
    current_process->wake_tick = target;
    current_process->state     = PROCESS_SLEEPING;
    process_yield();
    /* ... see full comment in the real commit for the reasoning ... */
    volatile uint64_t* vt = &total_ticks;
    while (*vt < target) {
        __asm__ volatile ("sti; hlt");
    }
    current_process->state = PROCESS_RUNNING;
}
```
Two things had to be confirmed before this was safe to write:
- **`total_ticks` is not declared `volatile`** (`kernel/proc/
  scheduler.c:12`). Without reading through a `volatile uint64_t*`
  inside the wait loop, the compiler could legally cache the read across
  iterations and never observe the timer IRQ's update — an infinite
  hang. Routed around by casting locally rather than widening the
  global's declared type everywhere it's used.
- **Interrupts are disabled for the ENTIRE syscall handler duration** —
  confirmed by reading `kernel/arch/x86_64/syscall_entry.asm` directly:
  `cli` fires immediately on entry, the only `sti` in the whole path is
  right before `sysretq` at the very end. So without explicitly
  re-enabling interrupts inside the wait loop, the timer IRQ could never
  fire and `hlt` would wait forever — real deadlock, not just a slow
  sleep. `sti; hlt` together (the same idiom already used in this file's
  own panic/crash halt loops) is the correct, atomic way to do this
  without a race between checking the condition and halting.

**Verified in QEMU, with real evidence, not just re-running the same
broken test:** `evtest.py` via `yourun` now genuinely paces over real
time (progressive output as time passed, confirmed by the person
watching it happen live) instead of finishing instantly, AND
`event.get()` now correctly captures real keypresses made during those
wait windows — confirmed via matching ASCII codes for the actual keys
pressed (32=space, 97/98/102/103/106/110/118 = a/b/f/g/j/n/v). No
regression observed in keyboard, mouse, or desktop animation behavior
after the change — checked explicitly, not assumed.

### 3. yougame module shipped (`fa3e6f9`)

Pygame-inspired MicroPython graphics module. `display.set_mode/flip`,
`Surface.fill`, `draw.rect/circle/line`, `time.wait/get_ticks`,
`event.get` — all genuinely verified working in QEMU this session (the
rendering half via screenshot, the time/event half only after the
scheduler fix above). New file
`third_party/micropython/ports/youos/modyougame.c` (~360 lines), added
to both `SRC_C` and `SRC_QSTR` in that port's Makefile (checked against
this project's own documented gotcha).

## THE SCHEDULER PROBLEM — full current-state documentation for next
## session's bug hunt

This is the section to read carefully before starting the bug hunt.

**Current architectural reality, confirmed this session, not assumed:**
YouOS's `kernel/proc/scheduler.c` contains a complete-looking
multi-process scheduler — `process_create()`, `process_t` with
`PROCESS_READY`/`PROCESS_RUNNING`/`PROCESS_SLEEPING`/`PROCESS_DEAD`
states, a circular `process_list`, `do_switch()` with real preemption
(`TIMESLICE`-based, `scheduler_tick()` decrements and calls
`do_switch()` on expiry), and `process_sleep()`/wake-on-tick logic. **All
of this machinery is currently dead code in the sense that it's never
actually invoked to create a second process.** Every program that
actually runs in this OS today runs through one of two totally separate,
simpler mechanisms:
1. Boot-time: `elf_load()` + `jump_to_userspace()` + `ksetjmp`/longjmp,
   directly in `kernel_main()`, for the initial `desktop`/`shell` launch.
2. Runtime nested execs (`yourun`, and presumably any other in-OS
   "run this program" feature): `sys_exec()`/`sys_exec_arg()`
   (`kernel/arch/x86_64/syscall.c`), a synchronous, same-context,
   CR3-switching + `exec_depth`-tracked nested call.

Neither of these touches `process_list` or creates a `process_t`. There
is, for all practical purposes, exactly **one** `process_t` in this OS's
entire runtime lifetime: pid 1, created once in `scheduler_init()`.

**This session's fix (`8baa16d`) patches around this reality for
`process_sleep()` specifically** — it now falls back to a real
`sti;hlt`-based wait when the cooperative yield turns out to be a no-op
(which is always, today). This is a correct, verified fix for
`process_sleep()` itself. **It does NOT fix the underlying architectural
gap** — it's a targeted patch for one symptom of a much larger fact
about this codebase that the next session needs to reckon with
directly: a whole scheduler subsystem exists, looks complete, compiles
clean, and does essentially nothing, because nothing calls into it the
way it expects to be called into.

**Open questions this raises, specifically for the bug hunt, not yet
investigated:**

- **Are there other places in the kernel that assume `process_list` can
  have more than one entry, the same way `process_sleep()` implicitly
  did?** `scheduler_tick()`'s "wake sleeping processes" loop
  (`kernel/proc/scheduler.c`, walks `process_list` checking
  `PROCESS_SLEEPING`/`wake_tick`) is currently walking a list of exactly
  one — harmless today, but worth grep'ing for every other consumer of
  `process_list`/`current_process`/`PROCESS_*` states to see if any of
  them has a similar silent-no-op or wrong-assumption bug waiting,
  the same shape as the one just found.
- **Is `MAX_PROCESSES` (`include/kernel/process.h`, `#define
  MAX_PROCESSES 16`) referenced anywhere with an assumption that's now
  false?** `do_switch()`'s search loop uses it as a bail-out bound
  (`if (++loops > MAX_PROCESSES) return;`) — fine as a defensive bound,
  but worth checking if anything else sizes an array or makes a decision
  based on an assumed process count that's never actually reached.
- **Is `process_create()` ever *meant* to be called somewhere, and
  something upstream silently fails to reach that call?** i.e., is this
  dead code because a feature was abandoned, or dead code because
  something that *should* call it has its own bug preventing it from
  ever getting there? Worth a targeted look at whether `yourun`/`sys_exec`
  were originally *intended* to create real processes and got
  simplified/shortcut at some point, vs. the scheduler being aspirational
  infrastructure that was never connected in the first place. This
  matters for deciding whether the long-term fix is "wire up the real
  scheduler properly" or "rip out the dead code and simplify."
- **`process_sleep()`'s fallback now always takes the `hlt`-loop path.**
  This is correct for today's single-effective-process reality, but a
  sleeping process now genuinely halts the WHOLE CPU rather than
  yielding to something else. If `process_create()` ever does get used
  for real concurrent processes in the future, this needs revisiting —
  flagged in AGENT_STATE.md, repeating here since it's directly relevant
  to scheduler bug-hunting scope.
- **Interrupts are `cli`'d for the full duration of every syscall
  handler** (`syscall_entry.asm`), with the single exception now being
  `process_sleep()`'s new fallback loop, which explicitly re-enables them
  mid-handler. Worth asking: are there OTHER syscalls that run long
  enough, or need to observe interrupt-driven state changes (keyboard,
  mouse, timer) mid-handler, where the same kind of gap could exist?
  Anything that busy-polls (`sys_keypoll` itself doesn't block, but does
  anything busy-poll-and-hope elsewhere?) is worth a look. Also worth
  checking: does holding interrupts off for a genuinely long-running
  syscall (if one exists) risk starving the keyboard/mouse IRQ queues
  long enough to matter in practice?

## Other candidates for next session's bug hunt (not investigated this
## session — found only as compiler warnings or incidental observations,
## never chased down)

These have been showing up in every single build this session and the
previous one, unchanged, never addressed. Worth treating "the build has
carried the same warnings for multiple sessions without anyone looking
at them" as itself a signal for where hidden bugs live — this session's
whole discovery started from exactly that pattern (something that looked
fine on the surface, never actually verified).

- **`kernel/fs/fat16.c`, TWO separate real undefined-behavior warnings**
  (`-Waggressive-loop-optimizations`, GCC's own words: "invokes undefined
  behavior"), at `root_find()` line 127 and `fat16_create()` line 436 —
  both are `for (int i = 0; i < 11; i++) name[i] = ...[i];`-style loops
  where GCC believes the destination array is smaller than 11 bytes
  before iteration 8. **This is GCC directly telling you there is likely
  a real buffer overflow in FAT16 filename handling** — highest-priority
  candidate for the bug hunt, since undefined behavior in filename
  handling could mean real memory corruption, not just a cosmetic
  warning. Start here: check the actual declared size of `entry_name`
  (line ~127 context) and `ne.name`/`name83` (line ~436 context) against
  the loop bound of 11.
- **`kernel/proc/scheduler.c`, `process_create()`:**
  `void* stack_page = pmm_alloc_page();` — "initialization of 'void *'
  from 'uint64_t' ... makes pointer from integer without a cast." Since
  `process_create()` is currently dead code (per the finding above), this
  hasn't caused a real bug yet — but if the bug hunt or a future session
  ever does wire up real multi-process support, this needs a proper look
  first: is `pmm_alloc_page()`'s `uint64_t` return a physical address
  that needs translation before being used as a kernel pointer, or is
  this actually safe on this platform and just missing an explicit cast?
  Don't assume; check `pmm_alloc_page()`'s real contract.
- **`kernel/drivers/uhci.c`, `proc_mouse()`:** `implicit declaration of
  function 'mouse_update_usb_delta'; did you mean 'mouse_update_usb'?`
  — a real function being called with no visible prototype. Either a
  missing header include, or (more concerning) a typo'd function name
  that happens to link successfully against some other symbol by
  accident, or a genuinely separate function that's missing its
  declaration. Worth confirming this resolves to the function it's
  supposed to, not silently calling the wrong thing via implicit
  `int`-returning declaration mismatch.
- **`kernel/arch/x86_64/syscall.c`, `sys_stat()` line 486:**
  `if(so)*(uint32_t*)so=sz;if(io)*(uint8_t*)io=isd;return 0;` — GCC's
  `-Wmisleading-indentation` flags that the second `if` LOOKS like it's
  guarded by the first but isn't (they're both on one line, no braces).
  Read this one carefully by eye — dense one-line syscall bodies are a
  style used throughout this file, so it's not inherently wrong, but
  this specific line is dense enough that it's worth double-checking the
  actual control flow matches what was intended, especially since it's
  live syscall code (`sys_stat`), not dead code like the scheduler
  warnings above.
- **`kernel/mm/vmm.c`, `get_or_create()`:** `unused variable 'pt_flags'`
  — likely harmless (a value computed and never used), but "computed and
  never used" in page-table code is worth a two-minute look: was it
  *meant* to be applied somewhere and the application got dropped by
  accident (a real correctness gap), or is it genuinely dead/leftover?
- **`kernel/arch/x86_64/kernel_main.c`:** `unused variable 'fat16_ok'` —
  `int fat16_ok = fat16_init();` with the result never checked. Similar
  question: is silently ignoring FAT16 init failure intentional
  (fallback path exists elsewhere) or a real gap where a failed FAT16
  mount goes unnoticed and something downstream assumes it succeeded?

**None of the above six were investigated this session** — they're
listed here purely because they're compiler-confirmed, already-visible,
multi-session-persistent signals, which makes them an efficient starting
point for a dedicated bug hunt rather than needing rediscovery from
scratch. The FAT16 undefined-behavior ones in particular look like the
highest-value place to start, given GCC is using the words "undefined
behavior" directly, not just a style nit.

## Longer-term items, unchanged, not touched this session

- **`ycfs_write()` is still not transactional.** Not a known bug, but a
  real architectural gap — no WAL protection for a crash mid-write.
  Would need reference-counted/nested transaction support since
  `ycfs_write()` is called from inside other functions' already-open
  transactions today.
- **`yougame` could be extended further:** images, text rendering, mouse
  events, sound — none started, and deliberately paused per the person's
  explicit request to prioritize the bug hunt over new features next
  session.

## Summary for whoever picks this up next

Two real, verified fixes landed this session on top of last session's
journal work: the journal-wipe perf optimization (small, contained,
filesystem-scoped) and the `process_sleep()` scheduler fix (larger in
implication — it's a patch for one symptom of a whole dead subsystem in
this kernel). The yougame module is genuinely done and tested, not just
claimed done this time.

The real work for next session isn't a feature — it's the bug hunt the
person explicitly asked for, motivated directly by this pattern
repeating across the last several sessions: a feature attempt trips over
a silent, pre-existing, unrelated kernel bug that turns out to be far
more consequential than the feature itself. This doc's "THE SCHEDULER
PROBLEM" and "Other candidates" sections above are the starting map for
that hunt — particularly the FAT16 undefined-behavior warnings (real
memory-safety risk, compiler-confirmed, never investigated) and the
open question of whether other kernel subsystems have the same
"looks wired up, isn't actually connected to anything" shape that the
scheduler turned out to have. Don't take any existing "this works"
assumption in this codebase for granted without a real QEMU test behind
it — that discipline is what found every real bug across the last three
sessions, including this one.
