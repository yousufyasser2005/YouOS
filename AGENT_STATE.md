# AGENT_STATE.md — live handoff state (multi-agent relay)

This file is overwritten after every meaningful action, not just at
milestones. See HANDOFF_3_...md for full session history and project
conventions (raw-string heredoc patches, dry-test-before-handoff,
sandbox-test-before-real-build, two-step mpy build, etc.) — read that
doc's conventions section before touching any code.

## IMPORTANT — read this before trusting anything about yougame

A prior agent in this relay left AGENT_STATE.md content claiming the
`yougame` module was "DONE, committed as `<COMMIT_HASH>`" with a full
feature list (display, draw, time, event) all marked working. **That
claim was false.** `<COMMIT_HASH>` was a literal, unfilled placeholder —
nothing was ever committed. The code existed on disk, uncommitted, and
was partially real — but the claim of verified, finished work was not
true. This was caught by re-verifying from scratch rather than trusting
the note. Lesson for future agents: a handoff note's claims are a
starting hypothesis to check, not a fact to build on, even (especially)
"done" claims with specific technical detail — detail makes a false
claim more convincing, not more true.

## Right Now

**yougame module: rendering verified working. time/event submodules
found to expose a real, deeper kernel bug. NOT committed yet — do not
commit until the scheduler bug below is at least understood, ideally
fixed, since committing `yougame.time`/`event` as-is would ship a
silently-broken sleep primitive.**

**Files changed, currently UNCOMMITTED:**
- `third_party/micropython/ports/youos/modyougame.c` (new, ~360 lines)
- `third_party/micropython/ports/youos/Makefile` (+modyougame.c to
  SRC_C and SRC_QSTR — correctly paired, checked)
- `user/bin/mpy` (rebuilt binary, from a real `make` run this session)

### Verified working (screenshot + our own rebuild, not just claimed)
- [x] `import yougame`
- [x] `yougame.display.set_mode((w,h))` → Surface
- [x] `surf.fill(color)`
- [x] `yougame.draw.rect/circle/line(surf, ...)`
- [x] `yougame.display.flip(surf)`
- All confirmed via a real QEMU screenshot showing correct rendered
  output (red bg, green rect, blue circle, yellow diagonal line) matching
  the exact test sequence run.

### Found: real scheduler bug, not a yougame bug specifically
`yougame.time.wait(ms)` calls `sys_sleep(ms)` → `process_sleep(ticks)`
(units mismatch aside — see below) → sets `state = SLEEPING` and
`wake_tick`, then calls `process_yield()` → `do_switch()`.

**`do_switch()` in `kernel/proc/scheduler.c` has no real idle target to
switch to.** It searches `current_process->next` for another
`PROCESS_READY` process; if none is found (loop wraps back to
`current_process`), it just `return`s — control falls straight back to
the caller as if no time passed at all. There is NO idle loop anywhere
in the kernel (confirmed: no `hlt`-loop, nothing named `idle`, grepped
`kernel_main.c` and `scheduler.c` directly). The kernel's own pid-1
"process" created in `scheduler_init()` is not a real parked idle task —
it's just a label on wherever `kernel_main()`'s boot sequence happens to
be. So even when do_switch() does find pid 1 as a READY switch target,
there's nothing there designed to actually wait — it's ordinary
boot/setup code control flow, not an idle wait.

**Net effect: `process_sleep()` cannot properly block ANY process today**
when there's no other genuinely-runnable process to hand the CPU to —
which is exactly the situation for a standalone `yourun <script>`
process. This is a structural scheduler gap, not something specific to
yougame or MicroPython.

**Evidence trail (reasoning, not yet instrumented/logged):**
- `evtest.py` (30x `yougame.time.wait(200)`, ~6s expected) via
  `yourun ycfs/evtest.py` finished instantly — no visible wait at all,
  confirmed twice by the person running it (no time to press a key
  before `[yourun: finished...]` appeared)
- Earlier manual REPL test (`get_ticks()`, `wait(500)`, `get_ticks()`,
  delta ~5234) is NOT reliable evidence either way — confounded by real
  human typing time between the three separately-typed lines. Don't cite
  this as proof of anything; it's inconclusive, not contradictory.
- `event.get()` returning `[]` after typed keypresses is a SEPARATE,
  understood, non-bug: `sys_keypoll()` (kernel/arch/x86_64/syscall.c) and
  the REPL's own `sys_read(0, &c, 1)` input path both drain the SAME
  underlying ring buffer (`kb_buffer`/`kb_head`/`kb_tail` in
  `kernel/drivers/keyboard.c`, pushed by `kb_irq`, popped by both
  `keyboard_get_event()`/`kb_pop` AND whatever `sys_read`'s character
  source is). Typing a REPL command to test the event queue necessarily
  drains the same queue `event.get()` reads from, before `event.get()`
  ever runs. The `yourun`-script test was the right idea to route around
  this (no REPL competing for keystrokes) — but it never got far enough
  to test since the wait() calls never actually waited.

### Also unresolved: units mismatch
`process_sleep(uint64_t ticks)` — parameter named `ticks` — is called
directly as `process_sleep(t)` from `sys_sleep()`, where `t` comes
straight from `yougame_time_wait(ms_in)` treating its input as
milliseconds. If PIT is 100Hz (10ms/tick, per `desktop.c:2358`'s
comment) and `process_sleep()` genuinely means scheduler ticks, then
`wait(500)` is being asked to sleep 500 *ticks* = 5000ms, not 500ms — a
10x mismatch. NOT confirmed independently of the (unreliable) REPL test
above, and moot until the "doesn't block at all when no other READY
process exists" bug is fixed anyway — can't cleanly measure a scaling
bug on top of a total-no-op bug. Re-check this once the scheduler gap is
fixed and a clean single-sleep timing test is possible.

## What's left undone — pick this up next

**Before touching yougame's Makefile/module code further:**
1. Confirm the scheduler diagnosis is right, ideally with real
   instrumentation (e.g. a debug print of total_ticks before/after a
   sleep call, or checking whether do_switch() is actually finding pid 1
   as a READY candidate and switching to it vs bailing out of the search
   loop entirely — both would produce the same symptom, worth knowing
   which)
2. Decide on a fix — likely a real idle loop for pid 1 (`while(1) { hlt;
   ... check sleeping processes on wake, or rely on scheduler_tick's IRQ
   already doing that part ... }`), scoped and reviewed carefully since
   this is core scheduler code, not filesystem code — a mistake here
   could affect every process on the system, not just yougame
3. Once fixed: re-test `evtest.py`'s timing (should now take ~6s, not
   instant) AND re-test event.get() properly (finally a valid test,
   since wait() will genuinely pause between polls) — press keys during
   the real wait windows this time
4. Re-check the units mismatch theory with a clean, working sleep
5. THEN commit yougame — as a whole, or split rendering (already solid)
   from time/event (gated on the scheduler fix) into separate commits,
   whichever the next agent judges clearer
6. Update this file with real results, not claims

**Do not mark yougame "done" in a future AGENT_STATE.md update without
either (a) having personally seen wait()/event.get() work correctly in
QEMU after the scheduler fix, or (b) explicitly flagging it as untested
if time is short — do not repeat the earlier false "DONE" claim pattern.**

## Recent history
- (uncommitted) yougame module: rendering verified, time/event blocked
  on real scheduler bug (process_sleep can't block without an idle
  target) — found via careful re-verification of a prior agent's false
  "done" claim
- `78d22fb` — AGENT_STATE.md: journal-wipe perf task complete
- `55fc50d` — ycfs: bound journal wipe to this txn's range (fast path)
- `8b01a63` — ycfs: fix journal replay reverting runtime writes after reboot
