# AGENT_STATE.md — live handoff state (multi-agent relay)

This file is overwritten after every meaningful action, not just at
milestones. See HANDOFF_3_...md for full session history and project
conventions (raw-string heredoc patches, dry-test-before-handoff,
sandbox-test-before-real-build, two-step mpy build, etc.) — read that
doc's conventions section before touching any code.

## Right Now

**Task:** journal-wipe performance optimization in `kernel/fs/ycfs.c`
(`txn_commit()` currently wipes the *entire* journal region on every
commit — 128 blocks / ~1-2s hang per file create/delete — when only the
blocks this transaction actually used need wiping).

**Status: plan finalized, not yet implemented. No code changes made yet
this sub-task.**

Blocking correctness question (from HANDOFF_3) has been resolved:
- [x] Read `journal_log()`'s real body (`kernel/fs/ycfs.c` ~line 167)
- [x] Confirmed: `journal_log()` **can** reset `journal_pos = 0` mid-transaction
      (wraps if `journal_pos + 2 > sb.journal_num_blocks`), called on every
      `write_block()` while a transaction is open — NOT just once at commit.
- [x] Confirmed this breaks the naive "`[0, journal_pos)` is always this
      transaction's full range" assumption HANDOFF_3 flagged as unverified.
      A naive optimization using only `[0, journal_pos)` would leave stale
      pre-wrap journal entries un-cleared — same failure shape as the bug
      fixed in `8b01a63`, reintroduced through a different path.

**Decided approach: option (1), wrap-tracking with safe fallback** (not
option (2), "grow journal_num_blocks so wraps are rare" — rejected as
papering over the correctness gap rather than fixing it):

- [ ] Add `static bool txn_wrapped = false;` near the other journal statics
      (`current_txn_id`, `journal_pos`, `next_txn_id`, ~line 163)
- [ ] In `txn_begin()`: reset `txn_wrapped = false;`
- [ ] In `journal_log()`: when the `journal_pos + 2 > sb.journal_num_blocks`
      branch fires, also set `txn_wrapped = true;` before the reset
- [ ] In `txn_commit()`: the existing `if (journal_pos + 1 > sb.journal_num_blocks)
      journal_pos = 0;` guard (for the commit record itself) must ALSO set
      `txn_wrapped = true;` when it fires — easy to miss, this is a second
      wrap site distinct from the one in `journal_log()`
- [ ] In `txn_commit()`: capture `journal_pos` into a local (`commit_pos`)
      right after writing the commit record, before it resets to 0
- [ ] Replace the wipe loop's bound: `wipe_count = txn_wrapped ?
      sb.journal_num_blocks : commit_pos;` — fast path (small wipe) when
      no wrap occurred this transaction, full-region wipe (today's
      already-proven-safe behavior) when one did
- [ ] Dry-test the diff against a reconstructed scratch copy of
      `journal_log()` / `txn_begin()` / `txn_commit()` before touching the
      real file (no host-gcc sandbox exists for kernel code — this means
      careful manual trace-through of the wrap/no-wrap cases by hand,
      not an automated run)
- [ ] Apply to real `kernel/fs/ycfs.c`
- [ ] Rebuild + QEMU test: (a) timing check — file create/delete should
      be near-instant now, not 1-2s; (b) re-run the ORIGINAL persistence
      regression test from `8b01a63` — write → close → restart (multiple
      methods) → read back — to confirm the perf fix did NOT reintroduce
      the data-loss bug
- [ ] If verified: commit with a message referencing both this fix and
      the `8b01a63` bug it's built on top of

**Next literal action:** write the actual C patch (heredoc), dry-test it,
then hand it over for real application.

## Recent history
- `5cc6a2c` — AGENT_STATE.md created (was committed with placeholder
  template text still in it, not real content — fixed by this write)
- `8b01a63` — ycfs: fixed journal replay reverting runtime writes on reboot
- `7e77335` — ata: fixed FLUSH CACHE not waiting for real completion
- `360a817` — mpy: real file write mode
