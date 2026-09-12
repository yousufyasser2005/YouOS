# YouOS — Handoff #3 (write mode shipped, real journal bug found+fixed, one perf follow-up)

**Path:** `/home/yousuf/codes/YouOS` · x86_64, C+NASM, Meson/Ninja (kernel),
Makefile (userspace), GRUB2, QEMU. Host: Arch-based (EndeavourOS),
`qemu-system-x86` 11.0.3-1 from pacman (genuine ELF, not sandboxed).

This doc hands off from an unusually long session that started as "add
write mode to `mpy`'s `open()`" and ended up finding and fixing a real,
pre-existing, deep kernel filesystem bug that had nothing to do with
MicroPython at all. Three commits landed this session (`360a817`,
`7e77335`, `8b01a63`, in that order, on top of the previous session's
`172a867`/`d35b886` real-`import` work). Read this doc's "What's left
undone" section before starting new work — there's one concrete, partially
designed follow-up waiting.

## Conventions (carried over from prior handoff docs, still apply)

- **No filesystem access to the user's machine.** All edits are
  `python3 - << 'PYEOF' ... PYEOF` heredocs (`assert old in s; s=s.replace(old,new,1);
  open(p,"w").write(s)`) using **raw strings** (`r'''...'''`) for `old`/`new`
  whenever the target text contains C escape sequences like `\0`/`\n` —
  this bit us once this session: a non-raw Python string interprets `\0`
  as an actual NUL byte, silently corrupting the target file. Always
  verify with a quick NUL-byte check after any patch:
  `python3 -c "print('NUL present:', b'\x00' in open(PATH,'rb').read())"`.
- **View the real current file before patching, every time**, and don't
  assume adjacent-looking code sections are actually adjacent in the real
  file — this also bit us twice this session (patches that assumed two
  known lines were next to each other, when 5-10 unrelated lines actually
  sat between them; caught cleanly by the `assert` failing, not by
  corrupting anything, but cost a round-trip each time).
- **Dry-test every patch against a reconstructed scratch file before
  handing it to the user.** This session built the habit of recreating
  the relevant few dozen lines of the real file locally, running the exact
  patch script against that scratch copy, and checking the result (no NUL
  bytes, correct diff, `py_compile`/`file` sanity checks where applicable)
  before ever handing the command over. Caught every mistake this session
  made before it reached the user's real files.
- **Sandbox-test anything algorithmic before it touches the real kernel/mpy
  build.** The MicroPython host-gcc sandbox at
  `third_party/micropython/ports/youos-sandbox/` (gitignored, intentionally
  not committed — see `.gitignore`) is real and working again after being
  rebuilt from scratch this session (it had been lost between sessions).
  It was extended this session to include a working `open()`/file-object
  implementation (fopen-backed instead of sys_open-backed) specifically to
  runtime-test the write-mode buffer-accumulation/flush-on-close logic
  under ASan/UBSan before it touched the real freestanding build. Rebuild
  instructions: `cd third_party/micropython/ports/youos-sandbox && make`
  (needs `../../py/mkenv.mk` and `../../py/py.mk`, which exist since the
  real port uses the same relative includes).
- **Build/boot commands** (unchanged, still accurate):
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
  **Important change from before:** `cache=writethrough` is now part of the
  standard boot command (added during this session's debugging — forces
  synchronous host writes, no QEMU-side write-back caching). Keep using it
  going forward; there's no reason to drop it and it costs nothing on a
  small dev image.
- **`mpy` build is two separate steps, easy to forget the first one:**
  editing `third_party/micropython/ports/youos/main.c` does NOT
  automatically rebuild `user/bin/mpy`. You must:
  ```bash
  cd third_party/micropython/ports/youos && make    # rebuilds mpycore.o
  cd ../../../../user && make                        # relinks bin/mpy
  ```
  *then* the usual `mkinitrd`/`ninja`/`grub-mkrescue` sequence. This bit
  the session once — `mkinitrd` silently packaged a stale `mpy` binary
  because only the kernel (`ninja`) had been rebuilt, not the MicroPython
  port itself.
- **`SRC_QSTR` gotcha** (from prior session, still true, didn't come up
  this session since no new `MP_QSTR_*` identifiers were introduced):
  any new file/identifier added to `SRC_C` in
  `third_party/micropython/ports/youos/Makefile` needs a matching entry in
  `SRC_QSTR` too, or the build fails late with a confusing "undeclared"
  error instead of failing cleanly at qstr-generation time.

## New: multi-agent relay workflow — read this before doing anything else

The person is now working across multiple AI agents on this project (this
Claude chat, future fresh Claude chats, and at least one other model),
handing off between them as message limits are hit — not simultaneously,
one at a time, relay-style. To make that reliable, there's a second file,
**`AGENT_STATE.md`**, at the repo root (committed to git). It's different
from this doc:

- **This doc (`HANDOFF_3_...md`) is a milestone snapshot** — written once,
  at a natural stopping point, and not touched again after.
- **`AGENT_STATE.md` is a live, constantly-overwritten file** — its
  "Right Now" section gets fully replaced after *every* meaningful action
  (a patch drafted, a patch applied, a test run, a decision made), not
  just at milestones. It's meant to capture whatever's in-flight that a
  git commit wouldn't show — the exact next literal action, any
  uncommitted changes, any open question that hasn't been verified yet.

**Any agent picking up this project — this chat, a fresh Claude chat, or
the other coworking agent — must do two things:**

1. **At the start of a session:** read `AGENT_STATE.md` first, then
   `git log --oneline -10` and `git status` to confirm the file's claims
   match reality (don't just trust it blindly — cross-check).
2. **Before ending a session, or before a handoff:** overwrite
   `AGENT_STATE.md`'s "Right Now" section with the exact current state,
   even if the current task isn't finished. Add one line to the "Recent
   history" list if something meaningful landed.

`AGENT_STATE.md` also contains the project's mandatory patching
conventions (raw-string heredoc patches, dry-test-before-handoff,
sandbox-test-before-real-build, the two-step `mpy` build, etc.) — these
apply regardless of which agent is doing the work, and skipping them is
exactly what caused real problems earlier this session (a corrupted file
from a non-raw-string patch, several failed patches from unverified
anchor text). Read that section before touching any code, every time,
regardless of how experienced the agent already feels on this project.

## What's done this session (commits `360a817`, `7e77335`, `8b01a63`)

### 1. `mpy` `open()` write mode (`360a817`)

`open(path, "w")` now actually works — `write()`, `close()`,
`with`-statement/`__exit__` flush, round-trip read-back, all
sandbox-proven then QEMU-confirmed. Fixed a real pre-existing bug along
the way: the mode argument was previously ignored entirely, so
`open(path, "w")` silently returned a **read** handle with zero error —
not something introduced this session, an old latent bug from when
`open()` was first stubbed in.

Design: `sys_save_file` (syscall 18 → `ycfs_savefile()`/FAT16 fallback at
the kernel level) is a **single-shot, whole-buffer** call — create-or-
overwrite the whole file at offset 0 in one go, no incremental
"append more bytes to an already-open path" primitive exists. So
`youos_file_obj_t` in write mode accumulates everything written into a
`vstr_t` (MicroPython's own growable buffer, already used elsewhere in
this file's REPL loop, no new dependency), and the real `sys_save_file`
call only happens once, at `close()`/`__exit__` time. **Nothing is
actually on disk until close() is reached** — a crash or hang before that
means nothing gets saved. Only `'r'` (default) and `'w'` modes are
supported; anything else (`'a'`, `'x'`, etc.) raises `OSError(EINVAL)`
rather than silently misbehaving — no append mode yet, deliberately out
of scope, easy to add later following the same pattern if ever needed.

**Real footgun worth remembering:** paths written via `'w'` mode must
include a literal `ycfs/` prefix to route to YCFS at all
(`path_is_ycfs()` in `kernel/arch/x86_64/syscall.c` requires it) —
anything without it silently falls through to a flat, basename-only
FAT16 fallback instead of erroring. Not auto-prefixed, matching this
port's own existing convention (`resolve_import_path`'s `ycfs/lib/`
fallback is written out explicitly too, never assumed).

### 2. ATA driver: real `FLUSH CACHE` bug, fixed but NOT the root cause of
the persistence bug (`7e77335`)

Found while chasing what turned out to be a completely different bug
(#3 below), but genuinely real and worth having fixed regardless:
`ata_write_sectors()`'s cache-flush step (`outb 0xE7` then
`ata_wait_busy()`) had two independent issues — (a) no settling delay
between issuing the flush command and polling status, a textbook
"read a stale BSY bit" ATA PIO mistake (the fix: call the
already-defined-but-never-used `ata_delay()`, the standard 400ns/4-status-
read settle, right before polling), and (b) the wait's return value was
silently discarded, so a timed-out flush was indistinguishable from a
successful one. **This fix alone did not resolve the persistence bug** —
confirmed directly via a clean write→close→restart→read-back test after
applying only this fix. Real bug, real fix, wrong theory about it being
the root cause of #3 below.

### 3. THE big one: YCFS journal replay reverting every runtime write on
any restart (`8b01a63`)

**Symptom:** any file written at runtime — via `mpy`'s new write mode,
via the shell's `cat`-adjacent write paths, via the desktop's Notepad/
Files apps (all confirmed to exhibit it) — would silently revert to
empty (`size: 0`, no data block allocated) after **any** kind of restart:
in-guest `reboot`, in-guest `shutdown`, closing the QEMU window, killing
the process externally. Directory entry always survived correctly; only
the file's actual inode content reverted. Provably NOT gone from
`disk.img` itself right before the restart (confirmed via direct
byte-level parsing of the raw disk image, completely bypassing the
kernel/QEMU) — something during the *next* boot was reverting it.

**Root cause:** `ycfs_create()` (called for any new file) wraps its work
in a journaled transaction — writes a *blank* placeholder inode, adds the
directory entry, commits. The journal entries for that transaction were
never cleared afterward — only `ycfs_journal_replay()` clears the journal,
and only at the very *start* of the *next* boot. Meanwhile, the actual
file content write (`ycfs_write()`, called immediately after
`ycfs_create()` returns, e.g. from `ycfs_savefile()`) correctly updates
that same inode with the real size/content — but *not* inside a
transaction, so the journal has no idea it happened. At the next boot,
`ycfs_journal_replay()` blindly re-applies every committed transaction it
finds — including that now-stale blank-inode write — stomping the
correct content right back to blank. The replay code's own comment says
this is meant to be safe because it's "idempotent — harmless even if the
real write already landed" — true only if nothing else has touched that
block since, which is exactly the assumption this bug violates.

**Confirmed, not just theorized:** a purpose-built forensic script,
`ycfs_inspect.py` (committed, at repo root — parses `disk.img` directly:
superblock, inode table, dirents, and now the journal region too, all
struct layouts taken verbatim from `include/kernel/ycfs.h`, completely
independent of the kernel) dumped the journal *before* a reboot and found
the exact stale `DESC`/`COMMIT` records for a test file's creation
transaction — target blocks matching the inode bitmap, superblock, the
file's own inode-table slot, the parent directory's dirent block, and the
parent directory's own inode — still sitting there un-cleared, right
before the later write that should have superseded them. This was also
independently confirmed by four separate fresh AI models given only a
symptom writeup (no code): three converged on "boot-time journal/recovery
reverting metadata" as the leading hypothesis before ever seeing source;
one, given the real source, independently derived the *exact* mechanism
above with no leading from this session.

**Fix applied:** `txn_commit()` now wipes the entire journal region
immediately after writing its own commit record, instead of leaving that
cleanup for the next boot's replay. Since `write_block()` already performs
its real, synchronous `raw_write_block()` immediately (write-ahead logging
here logs *before* the real write, it doesn't defer it), the transaction
is fully durable the instant `txn_commit()` returns — the journal serves
no further purpose after that point. Mirrors the same zero-fill idiom
`ycfs_journal_replay()` already uses at its own end, so no new pattern
introduced.

**Verified fixed:** write → close → restart QEMU (multiple restart
methods, multiple times, multiple filenames) → read back, now correctly
persists. Desktop Notepad and Files apps also confirmed working
afterward (they go through the same underlying write path, so this fixes
them too, not just `mpy`).

## What's left undone — pick this up next

### The one concrete, designed-but-not-implemented follow-up: journal-wipe
performance

The fix above wipes the **entire** journal region
(`sb.journal_num_blocks`, currently 128 blocks = 512KB) on **every single
`txn_commit()`**, regardless of how many blocks that particular
transaction actually used. This causes a noticeable **1-2 second hang on
every file create/delete** — 128 separate PIO block writes (each with its
own flush-and-wait cycle) where typically only 5-6 were actually needed.

**The optimization is already designed, just not verified/applied:**
since `journal_pos` resets to `0` after *every* commit under this fix, a
transaction's own journal entries always occupy exactly the range
`[0, journal_pos)` at the moment of commit (captured right after
`journal_pos += 1` for the commit record itself, before it gets reset to
`0`). So the wipe loop's bound should change from `sb.journal_num_blocks`
to that captured `journal_pos` value — zeroing only what this transaction
actually wrote, not the whole region.

**One correctness check blocks this, deliberately not skipped:** does
`journal_log()` — the function `write_block()` calls to log a `DESC`+data
pair during an *open* transaction — ever independently wrap `journal_pos`
back to `0` mid-transaction (e.g. if it hits the end of the journal region
before the transaction's own commit does)? If it does, the optimization's
"a transaction's entries are exactly `[0, journal_pos)`" assumption breaks
in that edge case, and entries from *before* the wrap would be missed by
a naive `[0, journal_pos)` zero. **`journal_log()`'s body has never
actually been read this session** — only inferred from its call site in
`write_block()` (`if (current_txn_id != 0) journal_log(current_txn_id, block_num, buf);`)
and from two `raw_write_block()` calls glimpsed once, early in the
session, that appear to belong to it (`raw_write_block(sb.journal_start_block + journal_pos, &hdr); raw_write_block(sb.journal_start_block + journal_pos + 1, data);`
— unconfirmed exact context, likely inside `journal_log()` itself but
never verified directly).

**To find it:**
```bash
grep -n "journal_log" kernel/fs/ycfs.c
```
Read the full function before touching anything. In practice, given the
current codebase only wraps `ycfs_create()`/`ycfs_unlink()`/
`ycfs_rename()`/`ycfs_chmod()`/`ycfs_chown()` in transactions (each
touching a small, bounded number of blocks — `ycfs_write()` itself is
**not** transactional at all, so large file writes can't currently blow
up a single transaction's journal usage), wraparound mid-transaction is
extremely unlikely to occur today — but "unlikely today" isn't the same
as "provably safe," and this is exactly the kind of thing worth getting
right before shipping, not after a rare wraparound silently reintroduces
a subtler version of the bug we just spent a whole session finding.

Once confirmed safe (or once `journal_log()` is adjusted to guarantee no
mid-transaction wrap, if that turns out to be needed), the actual
optimization patch is small — a few lines in `txn_commit()`, same
function this session's fix already touched. Sandbox/dry-test discipline
applies as always; this is kernel code so there's no host-gcc sandbox for
it (unlike the MicroPython work) — direct QEMU testing after a careful
manual code review is the available verification path, same as the
original bug's fix was tested.

### Longer-term items, unchanged from before (not touched this session)

- **`yougame`-style pygame-inspired binding.** Still blocked on the
  discovery pass named in the previous handoff doc: locating and reading
  `wm_new`/`px`/`text`/`rect` in the kernel source
  (`grep -rn "wm_new\|^void px\|^void text\|^void rect" kernel/ include/`).
  Not started this session at all.
- **`ycfs_write()` is still not transactional.** Not a known bug (nothing
  currently observed broken by this), but worth noting: unlike
  `ycfs_create()`/`ycfs_unlink()`/etc., a file content write has no
  journal protection at all right now. If a crash happens mid-write
  (data block written, inode update not yet persisted, or vice versa),
  there's no WAL protection for that specific window. This wasn't the
  bug this session found (that bug was about a *stale* transaction being
  wrongly replayed, not about *missing* transaction protection for
  writes) but it's a real, separate architectural gap noted by one of the
  external AI reviewers (Kimi) as a "more correct, more invasive" fix
  option — deliberately not implemented this session in favor of the
  smaller, lower-risk fix that was applied. Worth a deliberate decision
  later about whether it's worth the complexity (would need reference-
  counted/nested transaction support, since `ycfs_write()` is called from
  *inside* other functions' already-open transactions today, e.g.
  `append_dirent()`/`clear_dirent()`).

## Reference: `ycfs_inspect.py` (committed, repo root)

A read-only, host-side Python script that parses `disk.img` directly —
completely independent of the kernel or QEMU. Reads the superblock,
walks the root and `lib/` directories, dumps any named file's raw inode
fields (`size`, `direct[0..11]`, `blocks_used`, etc.) plus its data
block's raw bytes, and now also dumps the journal region (mirrors
`ycfs_journal_replay()`'s own scan logic — magic check,
`DESC`→`+2`/`COMMIT`→`+1` advancement — but only reads, never writes or
clears anything). Genuinely useful for any future filesystem debugging;
safe to run anytime, at any point in a QEMU session or between sessions.
Extend freely.

## Summary for whoever picks this up next

The write-mode feature works and is solid. The much bigger deal this
session was finding and fixing a real, previously-invisible data-loss bug
in YCFS's journaling that affected *every* write path in the OS, not just
MicroPython — confirmed with hard forensic evidence (not just code
reading) and independently corroborated by multiple fresh AI reviews.
The fix is deployed and verified working. The one clear next step is the
already-designed journal-wipe performance optimization, gated on reading
`journal_log()`'s real body first — don't skip that check even though the
current codebase makes the edge case unlikely; this project's whole
debugging discipline this session was "verify, don't assume," and that
discipline is exactly what found this bug in the first place.
