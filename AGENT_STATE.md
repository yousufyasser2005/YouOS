# AGENT_STATE.md — live handoff state (multi-agent relay)

This file is overwritten after every meaningful action, not just at
milestones. See HANDOFF_3_...md for full session history and project
conventions (raw-string heredoc patches, dry-test-before-handoff,
sandbox-test-before-real-build, two-step mpy build, etc.) — read that
doc's conventions section before touching any code.

## Right Now

**Journal-wipe performance optimization: DONE, committed as `55fc50d`.**

`txn_commit()` now wipes only `[0, commit_pos)` — the blocks this
transaction actually used — instead of the full `journal_num_blocks`
region on every commit. Falls back to the full-region wipe (the
already-proven-safe `8b01a63` behavior) if `journal_log()` or the
commit-record write itself wrapped `journal_pos` back to 0
mid-transaction, tracked via a new `static int txn_wrapped` flag.

- [x] Read `journal_log()`'s real body, confirmed it CAN wrap
      `journal_pos` mid-transaction (not just at commit) — this is why
      the naive `[0, journal_pos)`-only approach from HANDOFF_3 was
      rejected in favor of wrap-tracking with a safe fallback
- [x] Patch written, dry-tested against a reconstructed scratch copy of
      `journal_log()`/`txn_begin()`/`txn_commit()` first — clean diff,
      no NUL bytes
- [x] Applied to real `kernel/fs/ycfs.c`, diff reviewed, matched dry-test
      exactly
- [x] Kernel rebuild (`ninja`) clean — `fs_ycfs.c.o` compiled with zero
      warnings; all warnings in the build output belong to unrelated
      pre-existing files (scheduler.c, vmm.c, syscall.c, uhci.c,
      kernel_main.c, fat16.c), none touched this task
- [x] QEMU-verified: create/delete speed improved (no more 1-2s hang)
- [x] QEMU-verified: re-ran the ORIGINAL `8b01a63` persistence regression
      test — write file, close, full shutdown, fresh boot session, read
      back — content survived correctly. Confirms the perf fix did NOT
      reintroduce the journal-replay data-loss bug.
- [x] Committed as `55fc50d`

**This task is complete.** No open threads on it. Next agent picking this
up should treat `kernel/fs/ycfs.c`'s journal code as stable and correct
as of `55fc50d` unless something new turns up.

**Next literal action:** none queued. Pick from "Longer-term items" in
HANDOFF_3_...md — the `yougame`/pygame-binding discovery pass
(`grep -rn "wm_new\|^void px\|^void text\|^void rect" kernel/ include/`,
not started at all yet) is the most concrete unstarted item, or start a
fresh HANDOFF_4 doc if this session's about to end and there's enough
new material to warrant one.

## Recent history
- `55fc50d` — ycfs: bound journal wipe to this txn's range (fast path),
  full wipe on wrap — perf fix for the 1-2s create/delete hang,
  correctness gated on reading journal_log()'s real body first
- `e2d71fa` — AGENT_STATE.md: real content — journal-wipe perf plan (this
  was the fix to the earlier placeholder-only commit, `5cc6a2c`)
- `8b01a63` — ycfs: fixed journal replay reverting runtime writes on reboot
- `7e77335` — ata: fixed FLUSH CACHE not waiting for real completion
- `360a817` — mpy: real file write mode
