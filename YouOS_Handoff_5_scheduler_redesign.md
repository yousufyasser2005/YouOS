# YouOS — Handoff #5 (real multi-process scheduler landed end-to-end; one launch site + legacy cleanup left)

**Path:** `/home/yousuf/codes/YouOS` on the user's machine · x86_64, C+NASM,
Meson/Ninja (kernel), Makefile (userspace), GRUB2, QEMU. Host: Arch-based
(EndeavourOS), `qemu-system-x86` from pacman.

**GitHub:** `https://github.com/yousufyasser2005/YouOS` (public). Claude can
`git clone` this directly in its own sandbox to read/grep the real code —
no need to ask the user to paste files. **Claude cannot run the real build
or boot QEMU** — no cross-compiler (`x86_64-elf-gcc`), no `nasm`, no QEMU in
the sandbox. All actual compiling/linking/booting happens on the user's
machine, via commands Claude hands them.

**This doc exists because the previous chat session became too large
(token-heavy from pasted build logs, boot screenshots, and iterative
patch/test cycles) to continue efficiently. The work itself is in a good,
fully-tested, all-pushed state — this is a context handoff, not a
recovery-from-a-mess handoff.**

---

## Conventions this project runs on (read this before touching anything)

- **Claude has NO direct filesystem access to the user's real machine.**
  Claude clones the GitHub repo into its own sandbox to read/grep/reason
  about code, but every actual edit to the user's real files is done by
  the **user**, running `python3 - << 'PYEOF' ... PYEOF` heredoc scripts
  Claude writes, using `assert s.count(old) == 1` before every replace so
  a mismatch fails loudly instead of silently corrupting the file. Raw
  strings / careful escaping for anything with C escape sequences.
- **Every patch is dry-tested in Claude's own sandbox first**, against a
  copy of the real file, before being handed to the user: apply the same
  `s.count(old)==1` + `replace` logic, `diff` against the original to
  confirm the change matches intent exactly, check for NUL-byte
  corruption (`b'\x00' in open(path,'rb').read()`), and — critically —
  **syntax-check with `gcc -fsyntax-only -ffreestanding -I <headers-dir>
  -Wall -Wextra`** against a refreshed copy of the real headers. This
  isn't the user's real cross-compiler, but it reliably catches typos,
  type mismatches, and missing-declaration errors before ever handing
  code over, and has caught real mistakes multiple times this session.
- **Claude's sandbox clone goes stale easily — this bit us hard, more
  than once, this session.** The user's local repo and GitHub can diverge
  in either direction (unpushed local commits, or Claude's sandbox not
  pulling after a push). **Rule: before starting any new patch, run `git
  log --oneline -3` in the sandbox AND compare against what the user's
  last-reported `git log` said.** If they don't match, `git fetch origin
  main` and reconcile (usually `git reset --hard origin/main`, being
  careful to preserve any uncommitted sandbox-only work first via `cp` to
  `/tmp` or a saved diff) before proceeding. Skipping this check
  repeatedly caused wasted turns this session generating patches against
  the wrong base.
- **Every real behavioral claim in this session was verified by actually
  building and booting in QEMU on the user's machine** — screenshots or
  pasted serial output, not assumed. Several times a plausible-sounding
  hypothesis turned out to be subtly wrong (see "Corrections Claude had
  to make to itself" below) and was only caught because of this
  discipline. Keep it up.
- **Temporary test harnesses get stripped from `kernel_main.c` before
  committing**, once they've served their purpose and produced clear
  QEMU evidence (quoted in the commit message) — `kernel_main.c` should
  stay a clean boot path, not accumulate scaffolding. Two exceptions
  were deliberately kept **permanently** as reusable regression checks:
  `user/src/crashtest.c` (deliberately executes `UD2`, tests crash
  recovery) and `user/src/exectest.c` (calls `sys_exec()` twice via a
  real punched-CR3 caller, tests the exec mechanism end-to-end). Both are
  harmless dead weight in the initrd unless actually invoked.

### GitHub push auth (bit the user hard early this session)
Password auth is dead; needs a **classic** PAT (Settings → Developer
settings → Personal access tokens → Tokens (classic), `repo` scope,
explicit "No expiration" — fine-grained tokens don't support true
no-expiration and need per-repo access granted). `git ls-remote` succeeding
does NOT confirm a token works — public repos allow anonymous read
regardless of credentials. Only an actual `git push` proves it.

### Build / boot commands (current, includes all 8 initrd files)
```bash
cd /home/yousuf/codes/YouOS
python3 tools/mkinitrd.py iso/boot/initrd.img user/bin/hello user/bin/cat user/bin/shell user/bin/fbtest user/bin/desktop user/bin/mpy user/bin/crashtest user/bin/exectest
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
User's real boot flow to reach an interactive shell: **boot → login screen
→ desktop → open terminal → type `shell` to launch the real userspace
shell → `$` prompt.**

`mpy` build is still two separate steps (unchanged from prior sessions):
```bash
cd third_party/micropython/ports/youos && make
cd ../../../../user && make
```

---

## What this session actually was

Started as "verify a prior agent's claim was real" → became a full,
ground-up **redesign of the scheduler from dead code into a real,
multi-process, preemptible, crash-recoverable kernel** — the single
largest body of work across this project's history. Every phase was
independently designed, dry-tested, handed to the user, built on their
real toolchain, booted in QEMU, and confirmed via real evidence before
moving to the next. Full commit history, oldest to newest:

```
9e68ed1 bughunt: fix FAT16 struct UB, vmm huge-page-split permission bug, misc warnings
5f49649 sched: Phase 1 -- process_t gets a real address space, do_switch() is CR3-aware
145e314 sched: Phase 2 -- ring-3 now runs with interrupts enabled (IF=1)
5219bad sched: Phase 3a -- real process_create()-based ring-3 children
31e7d27 crash: recover real process_create() children, not just legacy sys_exec() chain
f02efc8 sched: reclaim dead process resources; fix kmalloc_aligned/kfree mismatch
fafa4ce sched: Phase 3 cutover -- sys_exec() now spawns real processes, not longjmp hacks
5916cbc security: remove broken user_process_t mechanism, replace with real spawn
2d7a187 sched: migrate boot-time desktop launch to process_create(); add real blocking
```
**`2d7a187` is the current HEAD, pushed, and confirmed working via direct
user testing (full desktop→terminal→shell flow, dragging windows, no
stutter).** Read each commit message in full (`git log -p` or GitHub) —
they're deliberately thorough and contain the actual reasoning, not just
a one-line summary. This doc summarizes; the commits are the real record.

### 1. Initial bug hunt (`9e68ed1`)
Before any scheduler work, a dedicated infra bug-hunt pass (per the
previous handoff's explicit request) found and fixed:
- **`fat16.c`: real undefined behavior** — two loops read/wrote 3 bytes
  past the declared bound of `name[8]` into `ext[3]` via packed-struct
  layout coincidence. GCC's `-Waggressive-loop-optimizations` warning
  ("invokes undefined behavior") was accurate, not cosmetic.
- **`vmm.c`: real memory-protection bug** — `get_or_create()` split huge
  pages using the caller's generic `flags` parameter instead of the
  original page's own permission bits (computed into `pt_flags`, never
  used). Could silently widen a kernel-only/read-only page to
  user-writable the instant it got split.
- **`fat16_vfs.c`: silent failure** — `fat16_vfs_mount()` returned a
  valid node unconditionally even if `fat16_init()` had failed. Added
  `fat16_is_initialized()`, gated the mount on it.
- Smaller: missing prototype for `mouse_update_usb_delta` (relied on
  implicit declaration matching by luck), two misleading-indentation
  false alarms (verified functionally correct, reformatted anyway).
- **Not yet fixed, logged as backlog** (user said polish later):
  `ata_init()`'s IDENTIFY-based drive detection is flaky/timing-dependent
  under QEMU — real PIO reads work independently and reliably regardless,
  so this only produces a misleading `[!!] ATA: no drive detected` log
  line sometimes, not an actual functional problem. Also: `bin/mpy` links
  with an RWX LOAD segment (linker warning) — real hardening gap, low
  priority. Also: `syscall_entry.asm`'s NASM "implicit DEFAULT ABS
  deprecated" warning, `idt.c`'s unused `exception_names` — cosmetic.

### 2. Phase 1 (`5f49649`) — `process_t` gets a real address space
`process_create()`/`do_switch()` existed but were **100% dead code** —
`process_create()` was never called anywhere, confirmed via full-tree
grep. Every real program launch went through `sys_exec()`'s synchronous
`ksetjmp`/`klongjmp` + manual CR3-swap nested-exec model instead (one
giant kernel context borrowing itself out to nested ring-3 excursions,
never real separate processes).

- Added `address_space_t as` to `process_t`.
- `process_create()`'s stack allocation moved from raw `pmm_alloc_page()`
  (PML4 index 0, the low identity map) to `kmalloc_aligned()` (PML4 index
  256, the kernel heap) — **necessary** because `vmm_create_user_as()`
  gives each process its own **private, independently-splittable** copy
  of the low identity map's page tables (correction below on what this
  means), and a naive raw-physical-address stack could end up pointing
  at memory whose mapping had diverged across processes.
- `do_switch()` reloads CR3 via `vmm_switch()` when switching to a
  process with a different address space.
- **Validated via a temporary two-process cooperative test** (kernel
  process + a process under its own `vmm_create_user_as()`, before ring-3
  or preemption were involved at all) — confirmed genuinely distinct
  `pml4_phys`, correct execution/writes under the isolated AS, and
  correct return to `kernel_as` afterward.

### 3. Phase 2 (`145e314`) — ring-3 can actually be preempted
Found via reading `usermode.asm`: `jump_to_userspace` hardcoded
`RFLAGS=0x002` (**`IF=0`**) for every ring-3 launch. Meant the timer could
**never** fire while any user program ran — not "scheduler doesn't switch
to a second process," but "the hardware literally cannot interrupt ring-3
code, at all, ever, by design," independent of whether a second
`process_t` existed. Flipped to `0x202` (`IF=1`).

De-risked by confirming `TSS.RSP0` (needed for a ring3→ring0
privilege-transitioning IDT gate) was already proven correct via
`crash.c`'s existing exception-recovery path (faults are synchronous,
IF-independent, so ring-3 faults already worked under the old regime) —
this change only added a new *trigger* (the timer) to an
already-working mechanism.

**Verified empirically, not just from reading the assembly**: added a
`spin` shell command (pure ring-3 busy loop, zero syscalls inside it, so
nothing could incidentally re-enable interrupts the way
`keyboard_getchar()`'s `process_yield()`→`sti` poll loop does — first
attempt at this test was contaminated by exactly that, worth remembering
if testing something similar again). Before: `delta=0` ticks elapsed,
every time, regardless of real wall-clock time in the loop. After:
consistent ~122-132 ticks across five runs (~1.2s at 100Hz).

### 4. Phase 3a (`5219bad`) — a real process can run ring-3 code end to end
- `process_t` gained `real_exit`, `user_entry`, `user_stack_top`.
- `process_ring3_trampoline()`: generic `process_create()` entry function
  that reads `user_entry`/`user_stack_top` off the current process and
  calls `jump_to_userspace()`.
- `sys_exit()` branches on `real_exit`: a real `process_create()`d
  process hands off via `process_exit()` (mark DEAD, `do_switch()`, never
  return) instead of the legacy `klongjmp`.
- **Two real bugs found and fixed getting the first end-to-end test to
  pass** (spawning `hello`, a real ELF, as a genuine child):
  1. `do_switch()` only reloaded `TSS.RSP0`, not the **separate**
     `kernel_stack_top` global `syscall_entry.asm` uses for
     `SYSCALL`/`SYSRET`-driven entry (TSS.RSP0 is IDT-gate/interrupt-only
     — these are two genuinely independent mechanisms). Fixed by syncing
     both together in `do_switch()`.
  2. The first test attempt ran *before* `syscall_init()` (which sets
     `EFER.SCE`, without which `syscall` is a guaranteed `#UD`). Crashed
     deterministically at the exact address of the child's first syscall
     instruction — confirmed via `objdump` against the real built
     binary, not guessed. Fix: run any real-child test after
     `syscall_init()`.

### 5. Crash recovery (`31e7d27`)
`crash.c` only recognized `kernel_exit_jmp_valid` (the legacy mechanism).
A real `process_create()`d child faulting fell through to the ring-0 halt
path and took down the whole machine over one process's bug. Added a
`real_exit`-checked branch (checked *before* `kernel_exit_jmp_valid`,
since that's a global that could be stale-true from pid 1's own context
while a different real child is the one actually crashing) that recovers
via `process_exit()`. Verified with `crashtest` (deliberate `UD2`): fault
caught, child reaped, **rest of OS kept running** — confirmed, not
assumed.

### 6. Resource reclamation (`f02efc8`)
Dead children leaked forever (process_t, kernel stack, address space) —
guaranteed eventual memory exhaustion under any real, repeated use.
`process_reap(child)`: unlinks from `process_list`, frees the kernel
stack, destroys the address space, frees the struct. Safe once caller has
observed `PROCESS_DEAD` (by then `do_switch()` has already completed a
real `switch_context()` away from it).

**Found a second, independent, previously-latent bug testing this**:
`kmalloc_aligned()` returned a pointer positioned *inside* a larger raw
`kmalloc()` block with no way to recover the real header — `kfree()`
assumes a header sits exactly `HEADER_SIZE` bytes before any pointer it's
given, true only for `kmalloc()`'s own pointers. Never caught before
because nothing had ever freed a `kmalloc_aligned()` pointer anywhere in
the codebase until `process_reap()`. Fixed by having `kmalloc_aligned()`
stash the raw pointer immediately before the aligned one it returns, and
adding **`kfree_aligned()`** — any `kmalloc_aligned()` result MUST be
freed with `kfree_aligned()`, never plain `kfree()`.

### 7. Phase 3 cutover (`fafa4ce`) — `sys_exec()` itself rewritten
The big one. `sys_exec()` — the syscall **every** real program launch
goes through (shell's `exec`, desktop's terminal-open and `yourun`) —
rewritten from the old `ksetjmp`/`klongjmp` + per-level static-array
nested-exec model to real `process_create()`/`process_reap()`. **No
changes needed to `user/src/shell.c` or `user/src/desktop.c`** — same
syscall number/convention, only the internal implementation changed, so
the real shell/desktop picked this up automatically.

Two more real bugs found via careful design tracing *before* writing
code (not discovered via crash):
1. `process_create()` dereferences its own `name` argument internally.
   The construction sequence needs `kernel_as` active (see below), so if
   `name` (a pointer into the *caller's* address space) were passed
   through raw, `process_create()` would dereference it under the wrong
   CR3. Fixed by copying `name` (and the optional exec-arg string) into
   local buffers *before* the CR3 switch, while still guaranteed
   dereferenceable.
2. Pid 1 (`desktop`, at the time still launched via the old
   `kernel_main.c` mechanism) had `stack_top == 0`, so `do_switch()`
   correctly skipped reloading `TSS.RSP0`/`kernel_stack_top` when
   switching back to it — fine until pid 1 spawns a real child via the
   new `sys_exec()` and that child gets reaped: `kernel_stack_top` would
   still point at the reaped child's *freed* memory, and pid 1's next
   syscall would run on a dangling stack. Fixed by giving pid 1 a real
   syscall-entry stack in `scheduler_init()` (matching
   `process_create()`'s own convention) — does **not** touch pid 1's
   `context.kernel_rsp` (its real, already-correct cooperative-switch
   stack, a completely separate concept).

Removed the now-fully-dead `exec_depth`/`exec_saved_*`/`exec_kstacks`/
`exec_arg_buf`/`MAX_EXEC_DEPTH` (8) entirely (confirmed unreferenced
elsewhere first). Real `process_t`-based nesting has **no equivalent
depth limit** — the end-to-end test actually nested three levels deep
(`kernel_main` → `exectest` → `hello`) with zero special handling.

**Important, carefully-verified-not-assumed fact**: `user_rsp_tmp`
(`syscall_entry.asm`'s own saved-user-RSP global) needs **no** manual
save/restore in the new model. The old need was purely an artifact of
`ksetjmp`/`klongjmp`/direct-`iretq` *bypassing* `syscall_entry.asm`'s own
normal symmetric entry/exit. The new model never bypasses it — it only
cooperatively suspends via `switch_context()`, which uses entirely
separate state (`context.kernel_rsp`).

### 8. Removed the broken `user_process_t` mechanism (`5916cbc`)
Found while auditing the remaining old-model launch sites:
`user_process_create()` mapped a **kernel-compiled function**
(`hello_main`) as ring-3-executable via `map_range_user()`, which looks
at a `current_user_as` pointer to pick the target address space — that
pointer was declared, referenced, and **never once assigned** anywhere.
Always fell through to `&kernel_as`. Meant every invocation permanently
mapped 2 pages of kernel code + the VGA buffer as user-accessible
directly into the **shared** kernel address space — a real
privilege-escalation-shaped hole. Separately, `user_process_t.as` was
never initialized (`vmm_switch()` on an all-zero address space would
almost certainly triple-fault on `CR3=0`). Removed entirely (redundant
with spawning the real `hello` ELF, which already demonstrates the same
thing correctly). The `"userspace"` command name was kept, now safely
implemented via the proven `process_create()` mechanism.

### 9. Boot-time launch migration + real blocking (`2d7a187`)
Migrated the boot-time `desktop`/`shell` launch off the old
`ksetjmp`+`jump_to_userspace` mechanism onto `process_create()` — same
pattern as `sys_exec()`. **This surfaced a real, user-reported
performance regression**: once `desktop` became a genuinely separate
`process_t` (not pid 1 itself), `process_list` had two real entries for
the first time during *normal* use. The existing "block until child
dies" pattern was a **busy** `while (child->state != PROCESS_DEAD)
process_yield();` loop — leaves the waiter `PROCESS_READY` the whole
time, so `do_switch()` treats it as an ordinary schedulable process, and
the timer forced a **real context switch (CR3 + TLB flush)** to pid 1
roughly every `TIMESLICE` (4) ticks — ~25×/second — even though pid 1
had nothing to do until `desktop` exited. Before this migration this was
free (single-entry list, instant no-op). User reported this as vague,
hard-to-describe stutter specifically during continuous rendering
(window dragging) — exactly what ~25Hz forced round-trips would produce.

**Fixed with a real `PROCESS_BLOCKED` state** (new enum value) and
`process_wait(child)`: sets the caller's state to `BLOCKED` (never
selected by `do_switch()`'s round-robin search — confirmed via reading
the existing search condition, no changes needed there) and yields.
`process_exit()` now checks, on every exit, whether anything is
`BLOCKED` waiting specifically for the exiting pid (new
`waiting_for_pid` field) and wakes it. All three busy-wait call sites
(`sys_exec()`, the boot launch, the `"userspace"` command) now use
`process_wait()`. **User-confirmed fixed** by direct testing (dragging
windows, moving the cursor) after this landed.

---

## Corrections Claude had to make to itself, mid-session (worth knowing)

1. **Early in Phase 1**, Claude incorrectly claimed `vmm_create_user_as()`
   *excludes* the low identity map (PML4 index 0) from every process's
   address space, and used that as the justification for a Phase 1 test
   design choice. **This was wrong.** The real behavior: PML4[0] is
   **deep-copied** (each process gets its own private, independently
   splittable page-directory copy, so one process's own ELF-load-induced
   huge-page split doesn't affect others) — not excluded. This is *why*
   VGA output and the low identity map already worked correctly for
   `sys_exec()`-launched programs throughout the project's history. The
   Phase 1 kernel-stack-to-`kmalloc_aligned()` change made at the time
   remains sound (better hygiene regardless), but the *stated reason*
   for it was wrong and was explicitly corrected mid-session rather than
   left uncorrected. If anything in the codebase or a future session
   references "PML4[0] is excluded from user address spaces," that's the
   incorrect claim — don't propagate it further.
2. Second Phase 3a test attempt's crash (`Invalid Opcode @ same address,
   twice`) initially looked like it could be the `kernel_stack_top` bug
   (fixed first, didn't resolve it) — turned out to be the *separate*
   `EFER.SCE`/`syscall_init()` ordering issue. Both fixes were real and
   necessary, just for different reasons; worth remembering that an
   identical, deterministic crash address across repeated runs is a
   strong signal to disassemble and check exactly what real instruction
   is there, rather than iterating on a plausible-but-unconfirmed theory.

---

## Current architecture (accurate as of `2d7a187`)

- **Every real program launch goes through `process_create()` +
  `process_ring3_trampoline()` + `process_wait()` + `process_reap()`.**
  This includes: `sys_exec()` (shell's `exec`, desktop's terminal-open
  and `yourun`), the boot-time `desktop`/`shell` launch, and the
  `"userspace"` demo command in `kernel_main.c`'s internal fallback
  shell.
- `process_t` real fields now: `pid`, `name`, `state` (READY/RUNNING/
  SLEEPING/DEAD/**BLOCKED**), `context.kernel_rsp` (cooperative-switch
  stack, via `switch_context()`), `as` (real per-process address space),
  `stack_base`/`stack_top` (syscall/interrupt-entry kernel stack, via
  `kmalloc_aligned()` — **free with `kfree_aligned()`, not `kfree()`**),
  `real_exit`, `user_entry`/`user_stack_top`, `exec_arg`,
  `waiting_for_pid`.
- `do_switch()` on every real switch: reloads CR3 (if the address space
  differs), `TSS.RSP0` (interrupt/IDT-gate entry), `kernel_stack_top`
  (separate global, `SYSCALL`/`SYSRET` entry) — all three, together,
  every time, if `next->stack_top != 0`.
- Pid 1 (`kernel_main()`'s own boot context) now has a real
  `stack_top`/syscall-entry stack too (set in `scheduler_init()`), same
  as every `process_create()`d child — no more special-cased "pid 1 has
  no stack" branch mattering in practice.
- **Real timer preemption works** in ring-3 (`IF=1` since Phase 2).
- **Real crash recovery works** for real processes (`crash.c`'s
  `real_exit` branch).
- **Real resource reclamation works** (`process_reap()`,
  `kfree_aligned()`).
- **Real blocking works** (`PROCESS_BLOCKED`/`process_wait()`) — waiting
  processes don't compete for round-robin timeslice preemption.

### Still using the OLD `ksetjmp`/`klongjmp` mechanism — the one remaining site
`kernel_main.c`'s own internal `exec ` command, inside its **normally
unreachable** internal fallback shell loop (only reached if both
`desktop` and `shell` are missing from the initrd, which never happens
in real use — but worth migrating for consistency and to finish killing
the legacy mechanism). Once this is done:

**`kernel_exit_jmp`/`kernel_exit_jmp_valid`/`ksetjmp`/`klongjmp`
(`kjmp.c`/`kjmp.h`) become referenced only by `sys_exit()`'s and
`crash.c`'s legacy fallback branches, which would then be permanently
dead** (nothing would ever set `kernel_exit_jmp_valid = 1` again).
Confirmed nothing else in the codebase uses `ksetjmp`/`klongjmp` at that
point — worth re-confirming via grep before removing, but this is a real,
concrete final-cleanup opportunity: removing `sys_exit()`'s and
`crash.c`'s dead legacy branches, and potentially `kjmp.c`/`kjmp.h`
themselves if truly nothing else needs a generic setjmp/longjmp.

---

## Explicitly deferred / not done (user's own stated priority: stability
## and performance first, cosmetic warnings during "final polishing")

- The one remaining launch site above, and the legacy `kjmp` cleanup that
  becomes possible once it's done.
- ATA detection flakiness (`ata_init()`'s IDENTIFY loop) — cosmetic
  misleading log line, not a functional bug, confirmed via repeated
  testing.
- `bin/mpy`'s RWX LOAD segment linker warning — real hardening gap, not
  urgent.
- `syscall_entry.asm` NASM deprecation warning, `idt.c` unused
  `exception_names` — cosmetic, from the very first bug-hunt pass.
- **True concurrent/backgrounded multi-program execution** (two
  different programs actually running side by side, not just one
  blocking on the other) — explicitly scoped OUT early in the scheduler
  redesign conversation as a separate, bigger UX/feature decision, not
  assumed as part of "fix the architecture." The current model still has
  every parent block on its child via `process_wait()` — this is
  deliberate, preserves existing UX exactly, and was never meant to
  deliver true backgrounding. If the user wants that, it's a new,
  explicit ask, not a natural continuation of this work.
- `ycfs_write()` not being transactional (flagged in an earlier
  session's handoff, never revisited this session — filesystem work
  entirely, unrelated to the scheduler).

---

## Summary for whoever picks this up next

The scheduler went from **completely dead code** to a **real,
preemptible, crash-recoverable, resource-reclaiming, properly-blocking
multi-process kernel**, verified at every single step by actually
building and booting on the user's real machine — not assumed working.
Every phase found at least one real, previously-latent bug specifically
*by being tested*, several of which would have caused silent corruption
or crashes under real use if shipped un-verified. The user cares
genuinely about this being a real daily-driver-grade OS, not a "looks
done" hobby project — match that bar. Keep the same discipline: dry-test
in sandbox first, `gcc -fsyntax-only` check, verify no NUL corruption,
hand off precise heredoc patches, and treat every QEMU boot as the real
test, not a formality. Don't assume prior claims (including this doc's)
are correct without verifying — that discipline is what caught every
real bug this session, including a mistake Claude itself made and had to
correct.
