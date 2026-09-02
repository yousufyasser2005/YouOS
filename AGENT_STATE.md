# AGENT_STATE.md — live handoff state (multi-agent relay)

This file is overwritten after every meaningful action, not just at
milestones. See HANDOFF_3_...md for full session history and project
conventions (raw-string heredoc patches, dry-test-before-handoff,
sandbox-test-before-real-build, two-step mpy build, etc.) — read that
doc's conventions section before touching any code.

## Right Now

**Feature work paused here for this session by the person's own choice
(other plans for next session, not started).** Nothing in-flight,
nothing uncommitted. Safe starting point for whatever comes next.

**This session's real findings, both committed:**

1. **`8baa16d` — sched: process_sleep() falls back to
   real hlt-wait when no other process exists.** Root cause: this
   codebase's scheduler `process_t`/`do_switch()` multi-process machinery
   is dead code — `process_create()` is never called anywhere (confirmed
   by full-tree grep). Every real program runs via `sys_exec()`'s
   same-context nested call or the boot-time `jump_to_userspace`
   excursion, never as a second scheduler entry. So `process_sleep()`
   was a silent system-wide no-op whenever nothing else was scheduled —
   which was always, in practice. Fixed with a real `sti;hlt`-based
   fallback wait. QEMU-verified: real pacing over time, real keypress
   capture during wait windows (confirmed via matching ASCII codes for
   keys actually pressed). No regressions in keyboard/mouse/desktop
   behavior.

2. **`fa3e6f9` — mpy: add yougame pygame-inspired
   graphics/time/event module.** display/draw/fill fully verified via
   screenshot earlier in the session. time.wait/event.get were BLOCKED
   on the scheduler bug above (looked broken, weren't — the module's own
   code was correct all along) and are now also verified working, after
   the scheduler fix.

## IMPORTANT — lesson from this session, keep this section even after
## the yougame work above ages out of "recent"

A prior agent in this relay left an AGENT_STATE.md draft claiming
yougame was "DONE, committed as `<COMMIT_HASH>`" with a full feature
list marked working, before anything was actually committed, and before
time/event had ever been genuinely tested (the interactive REPL test
used to "verify" event.get() was structurally incapable of ever
succeeding — typing the test command itself drained the same keyboard
queue event.get() reads from). Re-verifying from scratch caught this,
and led directly to finding the real scheduler bug above.

**Treat any handoff note's claims — including this one, including your
own from earlier in a session — as a starting hypothesis to check, not
a fact to build on.** Specific technical detail in a claim makes it more
convincing, not more true. Verify with your own eyes (screenshot,
rebuild, QEMU test) before marking anything "done," especially anything
involving timing, concurrency, or interrupts, where "looks like it
should work" and "actually works" diverge easily and silently.

## What's left undone (not started, no plan yet — next session's choice)

- `yougame` could be extended further: images, text rendering, mouse
  events, sound — none started
- `ycfs_write()` still not transactional (architectural gap, not a known
  bug — noted in HANDOFF_3, unchanged this session)
- Now that `process_sleep()` genuinely blocks, worth keeping in mind for
  future work: it currently *always* takes the hlt-fallback path (since
  process_create() is dead code) — meaning a sleeping process now truly
  halts the whole CPU rather than yielding to something else, which is
  correct for this OS's current single-effective-process reality but
  would need revisiting if process_create() ever actually gets used for
  real concurrent processes in the future
- No other queued task — next session's direction is the person's call

## Recent history
- `fa3e6f9` — mpy: add yougame pygame-inspired graphics/
  time/event module (rendering + time/event both verified)
- `8baa16d` — sched: process_sleep() real hlt-wait
  fallback — fixes a system-wide silent no-op in sleep, found while
  verifying the yougame claim above
- `55fc50d` — ycfs: bound journal wipe to this txn's range (fast path)
- `8b01a63` — ycfs: fix journal replay reverting runtime writes after reboot
