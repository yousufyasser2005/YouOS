# YouOS — Handoff #9 (storage/file-manager overhaul done; **copy still too slow and still glitchy**; next: fix that, then clipboard, Notepad, media)

Path: `/home/yousuf/codes/YouOS` on the user's machine · x86_64, C + NASM, Meson/Ninja (kernel), Makefile (userspace), GRUB2/Multiboot2, QEMU. Host: Arch-based (EndeavourOS), `qemu-system-x86`, `x86_64-elf-gcc` 13.2.0, `nasm`, host `gcc`, `python3`.
GitHub: `https://github.com/yousufyasser2005/YouOS` (public). **GitHub only has `dc77d48` (old). The user's work since then is in LOCAL, UNPUSHED commits plus uncommitted v4/v4a files — see §0.**
Source-tree version marker: file `VERSION` (currently "v4a").

---

## 0. FIRST THING TO DO IN THE NEW CHAT (the sandbox starts empty)

Claude's sandbox can clone GitHub, but GitHub is ~15 patches behind the user's machine. Ask the user to do ONE of these before anything else:

**A (preferred) – commit and push**, then Claude clones the real state:
```bash
cd /home/yousuf/codes/YouOS
git add -A -- VERSION rebuild.sh include kernel tools tests user/Makefile user/lib user/src .gitignore
git status --short | head -40        # sanity check: only source files, no binaries / .tar / .img / .bak
git commit -m "v4a: ATA bus-master DMA writes (+WRITE MULTIPLE fallback), copy pacing, kernel link check"
git push origin main
git log --oneline -5                 # paste this output to Claude
```
**B – upload** `~/Downloads/youos_tree_v4.tar` and `~/Downloads/youos_fix_v4a.tar` (Claude: `git clone` GitHub, `tar -xf` v4, `tar -xf` fix_v4a, then `sha256sum -c MANIFEST.sha256`).

Then ask the user for three diagnostics (they decide what the next step is — see §4):
1. The **first ~45 lines of the QEMU terminal** after a fresh boot (does it show `multiple-sector writes: 16 sectors/block` and `DMA writes: enabled (self-test passed)` under `ATA drive detected`?). **This was never confirmed for v4 — we do not even know DMA was active in the last test.**
2. The **`[xfer]` lines** printed in that terminal during a hands-off copy of `/test`.
3. A **screen recording** (mp4) of a hands-off copy (analysis recipe in §8).

Local git history on the user's machine: `dc77d48` (= origin/main) ← `5a4dc43` rtc ← `fe8df5e` storage + file manager (v3 content) ← (uncommitted: v4 DMA + v4a fixes).

---

## 1. How this project works (read before touching anything)

**Claude cannot build the kernel or boot QEMU** (no cross-compiler, NASM, QEMU or GRUB in the sandbox). The user builds and boots; Claude verifies everything it can on the host. The sandbox has host `gcc` 13, `python3`, `ffmpeg`, `numpy`, `Pillow`. **The sandbox can be reset mid-conversation** (it happened twice: working copy and /tmp vanished). Keep deliverables in `/mnt/user-data/outputs`, which survived.

### Delivery format (hard-won — follow exactly)
* **Cumulative archive `youos_tree_vN.tar`** + small fix archives `youos_fix_vNx.tar`. Each contains every file Claude changed (relative to GitHub `dc77d48`), a `VERSION` file, `rebuild.sh`, and `MANIFEST.sha256`. The user extracts it over the repo and runs `sha256sum -c MANIFEST.sha256`.
* **Give every delivery a UNIQUE filename** (`..._v5.tar`). Browsers save a re-download as `name (1).tar` and `tar -xf name.tar` silently extracts the OLD one; the manifest then still "passes". This wasted two test rounds. `rebuild.sh` now refuses to run if the tree lacks the newest feature marker — **update that gate (`ata_write_dma` today) when a new archive adds a feature.**
* **Do not use `.patch` files for anything big.** A generated patch was corrupt (made from a reset working tree), applied *cleanly* on the user's tree and silently reverted two source files. Cumulative archives with checksums avoid this.
* **Always give the rebuild command** and the QEMU command (the user asked for this explicitly, repeatedly). The user's rebuild is `bash rebuild.sh` (see §6).
* Every reply that delivers files must say how to verify the right build is running (`[desktop] build <stamp>` on the serial terminal and bottom-left of the transfer window; `VERSION`).

### Verification discipline
* **Run the host suite before handing anything over:** `bash tests/host/run_host_tests.sh` (~60–90 s, ~170 checks, must end `ALL HOST TESTS PASSED`). It runs the REAL kernel sources (`ycfs.c`, `ata.c`) and `desktop.c`/`fs_tree.h` on the host against file-backed fake disks and mock controllers.
* **Mutation-test new tests**: break the mechanism on purpose, confirm a test fails, restore the file. Several tests were found vacuous this way (e.g. a "self-test" that rewrote a sector with its own contents could never notice a dropped write).
* **Syntax checks with the real flags:** kernel `.c`: `gcc -fsyntax-only -ffreestanding -I include -Wall -Wextra f.c`; user `.c` (run inside `user/src`): `gcc -fsyntax-only -ffreestanding -fno-stack-protector -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -O2 -nostdlib -fno-pic -mcmodel=large f.c`. Userspace has **no SIMD / no float**. Real kernel flags (meson): `-O2 -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -mcmodel=kernel`, link `-Wl,--no-undefined`.
* `tests/host/link_check.sh` approximates the kernel link (it caught the `ycfs_write_chunk` class of error). With the host compiler it deliberately ignores `memcpy/memset/...` (newer host gcc emits calls the cross gcc doesn't); with `x86_64-elf-gcc` installed it is exact. In `rebuild.sh` it is a warning only — **an approximation must never block the user's build** (it did once, falsely).
* **Syscall/UI/boot behaviour is only verified when the user boots and reports.** Say plainly what is verified vs inferred.

### Mistakes to NOT repeat (all happened this stretch)
* Theorising before measuring: the disk cost model was wrong three times (fixed per-call cost → per-KB cost → per-*sector* host sync). Read the user's `[xfer]` lines / recordings first.
* A stray `git reset --hard && git clean -fd` on its own line ran in the wrong directory after a failed `cd` and wiped the working tree. **Never put destructive git commands where a failed `cd` can redirect them; use `set -euo pipefail` in scripts and absolute paths.** Inversely `set -e` in an ad-hoc test aborted before the restore step once — always restore in a trap/after an explicit `|| true`.
* The tool shell is `dash` (`sh`): no `<(...)`, no `{a,b}` expansion, no `PIPESTATUS`. Write real `#!/bin/bash` scripts.
* Files passed to `present_files` become visible to the user immediately — only present after testing.
* The user's QEMU runs **`cache=writethrough`**: every guest write is a synchronous host write. That dominates disk performance (§4).
* Don't trust earlier diagnoses (including this document's) without re-reading code.

### Useful analysis recipe for the user's screen recordings (they send mp4 at 1920×1080/60fps)
Guest screen sits at about x=88..1832, y=55..1036. Per-frame change at 960×540 with a pixel-count threshold (`abs(diff)>40`, ≥6 pixels) gives true screen-update cadence; crop+upscale the progress window to read its numbers.

---

## 2. What changed since Handoff #8 (all in the user's tree; all host-verified; boot-verified except where noted)

**Time:** `kernel/drivers/rtc.c` (CMOS RTC; LBA-independent), `SYS_RTC`=51. Sidebar/taskbar/login clocks show real time, real date, correct calendar; Settings 12h/24h and show-seconds toggles now work. *Still missing: calendar month navigation; timezone (shows CMOS value as-is; QEMU default is UTC; `-rtc base=localtime` shows local).*

**Syscalls (table now ends at 54, `SYSCALL_COUNT`=55; next free = 55):** 51 `SYS_RTC`, 52 `SYS_FSEEK`(fd,off,whence), 53 `SYS_FSIZE`(fd), 54 `SYS_SAVECHUNK`(path,buf,len,offset: offset 0 creates/empties the file, later offsets continue it). `vfs_seek/vfs_size`, FAT16 seek + one-sector FAT cache. `seektest <path>` terminal command opens a window running `user/src/seektest.c` (332 checks passed on a 20 MB YCFS file on the real kernel).

**YCFS v2** (`include/kernel/ycfs.h`, `kernel/fs/ycfs.c`): double-indirect blocks → files up to ~4 GB; multi-block bitmaps → filesystem sizes up to 16 TB; persistent allocation hints; 48-bit LBA in the ATA driver (so disks >128 GB); inode count scales (1 per 32 KB); `unlink` refuses non-empty folders; `rename` refuses moving a folder into itself; `savefile` shrinks on overwrite (a real old bug: saving a shorter note left the old tail); `ycfs_truncate`; `ycfs_write_chunk`; unlink frees data outside the journal (huge files can't blow the 128-block journal). **Tools:** `tools/ycfs.py` (`mkfs/info/ls/put/get/mkdir/rm/fsck [--repair]/export/import/migrate`; shut QEMU down first; refuses if the journal is unclean), `tools/mkycfs.py` (now a safe wrapper: refuses to erase without `--yes`). The user's `disk.img` was migrated in place: 4 GB sparse image, FAT16 in the first 64 MB, YCFS v2 from LBA 131072 (3.9 GB, 129,024 inodes, data from block 4197, 128-block journal). `fsck` after the last session: `56 inodes, 11766 blocks in use -> OK`. `/lost+found` holds `#40` (61 bytes recovered from the old "cut a folder orphaned its contents" bug). `tools/ycfs_inspect.py` is **stale for v2** (use `ycfs.py info/fsck`).

**Disk write path (this is where the performance work lives — §4):** `ycfs_write` batches (block reservation pool, exact pointer-block need, deferred pointer writes, contiguous runs of up to 31 blocks per ATA command); journal logging writes descriptor+data as one 2-block command and wipes in multi-block commands; `kernel/drivers/ata.c`: one cache flush per command (not per sector) with the spec-required 400 ns settle delay after each sector; **WRITE MULTIPLE** (16 sectors/DRQ block) enabled at boot after a self-test; **bus-master DMA writes (v4)** enabled at boot after a self-test (§4). Reads are still PIO, one sector per DRQ.

**File manager / desktop (`user/src/desktop.c`, `user/src/fs_tree.h`):**
* Cut+Paste = one `sys_rename` (instant, folder keeps contents). Copy = background **job engine** (`ft_job_*`): scan pass (counts, refuses >255 entries per folder / >6 levels deep *before creating anything*), then copy pass, chunked, with cancel and cleanup of partials; Delete of a folder = background job (no entry limit, ≤24 levels, indeterminate progress bar for huge folders, "no progress" guard so a stuck delete can't hang the GUI). Single-file delete = one quick unlink.
* **Transfer window** (`WIN_TRANSFER`=10): progress bar, sizes, speed/ETA, file count, a grey diagnostic line (`step <chunk> / <ms>`), build stamp bottom-left; minimizable; closing it cancels; notification on finish.
* Notepad opens/saves the **full path** it was opened from (old bug: always `/ycfs/<name>`, so files in subfolders showed empty and saves landed in the root folder).
* **Window slot leak fixed:** closing never freed a slot (`MAX_WINDOWS`=12); after 12 openings nothing could open until reboot. `wm_close()` now frees file manager/notepad/calculator/terminal/media-player slots (`WIN_FREE`), clears per-slot state, and both close paths use it.
* Pacing engine in the desktop loop: cycle-counter clock (calibrated against the tick; **kernel syscalls run with interrupts off, so the 100 Hz tick undercounts during long disk operations — never budget with `sys_ticks`**), chunk size adapts (double while a round trip stays under the "hitch limit", halve otherwise; 16 KB…1 MB), duty-cycle limit while the user is active (one slice, then ≥3× its length of rest), bigger slices after 600 ms of no input.
* Per-second **`[xfer]` diagnostic line** printed with `sys_write(1,…)` (and `[desktop] build <stamp>`, `[xfer] cycle counter: N kHz` once). **See §4: this may itself cause the flicker.**
* `date` terminal command prints raw RTC data (diagnostic).

**Tests/tools added:** `tests/host/` — `run_host_tests.sh`, `ycfs_kernel_harness.c`, `fs_tree_test.c`, `tree_check.py`, `repair_test.py`, `ata_mock_test.c` (+`gen_ata_mock.py`, `vga_stub.c`: spec-modelled IDE controller incl. WRITE MULTIPLE and bus-master DMA with PRD rule checks and failure modes), `window_slots_test.c`, `render_transfer.{c,sh}` (renders the real transfer window to PNG without booting), `link_check.sh`. `rebuild.sh` (6 stages + verification that the freshly built desktop is embedded in `youos.elf`).

---

## 3. Current facts worth knowing

* Framebuffer 1920×1080×32; `desktop.c` is one process (~5,900 lines), software-rendered into a static buffer then `sys_fbwrite` (8 MB per frame); baseline frame ≈ 40–55 ms (~20 fps) with nothing running. `.bss` ≈ 48 MB. `MAX_WINDOWS`=12. The desktop loop ends each frame with `flush(); sys_yield();`.
* **Syscalls enter with interrupts disabled** (`FMASK`=0x200 and `cli` in `syscall_entry.asm`; `sti` only on exit). Every disk command is therefore polled and blocks the whole machine (single core, no preemption inside a syscall) for as long as the drive takes.
* **`sys_write(1,…)` from the desktop process goes `vga_puts` → framebuffer console (`fb_terminal_*`) AND serial.** That means console text is drawn onto the real framebuffer.
* QEMU command (user's):
```bash
qemu-system-x86_64 -drive file=disk.img,format=raw,if=ide,cache=writethrough -cdrom build/youos.iso -m 256M -boot d -serial stdio -display gtk,zoom-to-fit=on -device isa-debug-exit,iobase=0x604,iosize=0x02 -device piix3-usb-uhci -device usb-tablet -netdev user,id=n0 -device rtl8139,netdev=n0,mac=52:54:00:12:34:56 -audiodev pa,id=snd0 -device AC97,audiodev=snd0 -enable-kvm -cpu host
```
* Cycle counter on the user's CPU: ~2.17–2.30 GHz. Boot message `YCFS journal: replayed pending transaction(s)` appears on **every** boot — it is the journal self-test deliberately simulating a crash; harmless.
* Limits: file manager lists only 64 entries per folder (`MAX_FILES`), names ≤31 chars shown; Notepad's Open dialog lists the root folder only; copy refuses folders with >255 entries or >6 nested levels; a file is ≤ ~4 GB (no triple indirect); YCFS still starts at a fixed 64 MB offset (needs a partition table for real installs).
* Journal design weakness (pre-existing, not fixed): a transaction's real block writes happen right after each block is logged, before the commit record exists, so a crash mid-transaction can leave partial updates that replay will not apply.
* Real-hardware reality check (roadmap A): storage is legacy IDE (PIO reads, DMA/PIO writes), USB is UHCI-only, mouse is a PS/2/USB tablet path — many modern machines have only AHCI/NVMe, xHCI, I²C touchpads. Boot only ever tested in QEMU.

---

## 4. TOP PRIORITY — the copy task still blocks/glitches the desktop and is far too slow

### The user's requirement (verbatim intent)
Copying must be **fast** and must **not affect screen rendering or desktop navigation at all**. Currently: "it still makes the same screen flickers while copying even when I leave it alone, and takes ~2 minutes to copy the test directory" (`/test` ≈ 22 MB: a 20 MB `big.bin`, `ding.wav` ≈ 290 KB, small jpg/png, a `.ymjp` video). Acceptable target: a few seconds, with the UI never hitching.

### Measurement history (same folder; each row is a different build; numbers are the user's)
| Build | Hands off | Working normally | Observed cost model |
|---|---|---|---|
| first job engine (8 KB chunks) | ~25 min (13 KB/s) | — | ~150 ms per call regardless of size |
| v2 big chunks, PIO plain writes | ~1.5–2.5 min (~130 KB/s) | 4–5 min | ≈100 ms + 6.4 ms/KB (≈4 ms per **512-byte sector** = one synchronous host write each) |
| v3 WRITE MULTIPLE (16 sectors/block) | 52 s | 2 min 31 s | ≈40 ms + 1.8 ms/KB; ~520 KB/s per round trip |
| **v4 DMA writes** | "~2 min" with some idle + actions; **flicker even hands-off** | — | **UNKNOWN — user never sent the boot lines or `[xfer]` lines for v4. DMA may not have been active.** |

### Why disk I/O is slow here
`cache=writethrough` makes QEMU do a synchronous host write for each *unit the guest sends*: WRITE SECTORS → per sector; WRITE MULTIPLE → per ≤16 sectors; **bus-master DMA → one per command (≤255 sectors)**. That's why DMA was built (§2). Reads are PIO and uncached on the host side (cheap).

### Why the UI still hitches (structural)
Every disk operation is a synchronous syscall with interrupts off: the whole machine waits. The job engine can only slice the work into smaller blocking pieces; any piece costs UI time. Plus ~4–5 metadata writes per `ycfs_write` call (bitmap claim, superblock, pointer block, inode, + journal in transactions).

### Suspects for the *flicker* (test these FIRST, cheapest first)
1. **The `[xfer]` diagnostics themselves.** `sys_write(1,…)` draws on the framebuffer console (§3). Once per second during a job a text line is painted into the real framebuffer and stays until the desktop's next `sys_fbwrite` — a ≤50 ms flash at the top-left, once a second, *even hands-off*. May also scroll the console (8 MB memmove). **Code path verified; the effect is NOT yet confirmed** (a recording of a user-interacting copy did not isolate it; the frames analysed just showed the user dragging a window). Test: ship a build with the prints disabled (or routed to serial only — e.g. a new syscall 55 `SYS_DBGLOG` writing to COM1, or `outb` to port 0xE9) and compare a hands-off recording. Keep the diagnostics available via serial only.
2. Frame hitches from long blocking disk syscalls (long frames, not partial redraws): confirm with `[xfer] frames=… max=…ms` lines.
3. Other once-per-N repaints: the file manager auto-refresh loop, `notif_add` popups, the transfer window's own redraw (not yet inspected).
4. QEMU/GTK display update cadence with a 8 MB `fbwrite` per frame (baseline cost; not copy-specific).

### Design for the real fix (agreed direction, in this order)
**Stage 0 (diagnose):** get boot lines + `[xfer]` lines + hands-off recording for v4; remove/redirect the on-screen prints; check whether `DMA writes: enabled` appeared. If DMA self-test said "not used", find out why (PCI class/prog-if lookup, BAR4, bounce-buffer physical address checks — `ata_enable_dma()` in `kernel/drivers/ata.c`).
**Stage 1 (reduce UI-blocking per byte):**
* *Write-behind session in the kernel*: keep a persistent pool of claimed blocks and defer inode/superblock/pointer writes across successive `SAVECHUNK` calls of the same file (flush on close/other-op/shutdown). Target ≤1–2 disk writes per 128 KB instead of ~6.
* Make reads cheaper (READ DMA; read several blocks per command; avoid re-reading the inode per call).
**Stage 2 (true non-interference):** **asynchronous DMA** — a submit call that programs the PRD and returns immediately, completion checked once per frame (or by an IRQ14 handler; PIC unmask + `nIEN` semantics need care), so the machine never waits on the disk. Needs: an in-kernel I/O state machine for a chunk (data DMA → metadata → inode), a rule that any other ATA operation first waits for in-flight DMA, and a desktop pump that calls begin/poll instead of one blocking call. Alternatives considered and judged much bigger: preemptible syscalls + a copier process (needs a filesystem mutex and blocking/wake primitives in the scheduler).
**Stage 3 (polish):** fast path when the machine is idle; progress UI unchanged.
Success criteria: hands-off copy of `/test` in seconds; frame `max` in the `[xfer]` lines stays ≈ baseline (≤ ~70 ms) during a copy; no visible flicker in a recording; host suite still green; new host tests for any new kernel I/O state machine (extend the mock controller; mutation-test it).

---

## 5. ROADMAP (agreed; status as of now) — checklist

**A. Harden what exists**
- [x] Real-time clock, real date/time/calendar
- [x] Storage correctness: seek/size, YCFS v2, fsck/repair, journal self-test, host regression suite for storage/file manager/ATA/desktop window logic
- [ ] **Copy/delete must not block or glitch the desktop; copy speed acceptable** (§4)  ← next
- [ ] Formal in-OS regression runner (formalise `crashtest/exectest/spawntest/iotest/seektest` + `meminfo/ipcinfo/proccount` into one runner with pass/fail)
- [ ] Real-hardware boot (everything so far is QEMU-only): AHCI/NVMe or at least verified legacy IDE fallback, xHCI/USB or PS/2, input devices, partition table/installer, ACPI power
- [ ] Graceful-shutdown signal; unclean-shutdown handling (journal hardening, see §3 weakness)
- [ ] ATA: SET FEATURES transfer-mode for real drives; interrupt-driven (not polled) I/O

**B. Daily-use features**
- [ ] **System-wide text clipboard** — separate item, spec in §7
- [ ] **Notepad improvements** — the user will list these in the new chat (do not guess; ask)
- [ ] Software installation story (everything is baked into the initrd)
- [ ] File manager: >64 entries per folder, long names, Open dialog in Notepad for subfolders, rename/move UI polish
- [ ] Calendar month navigation; timezone setting
- [ ] DHCP/DNS/HTTP, power management/ACPI, file-permission enforcement

**C. Architectural maturity**
- [ ] SMP (single core today), swap/demand paging, USB hotplug, stack protector/ASLR
- [ ] Preemptible kernel work (would also give the copy a clean home; see §4 alternatives)

**D. Polish/deliverable**
- [ ] Bootable USB installer, architecture document, scripted demo
- [ ] Optional UI items: UI-scale option for window contents/taskbar; cursor size at 1080p; **dirty-rectangle rendering** (the desktop redraws all 1080p every frame, ~40–55 ms; this caps responsiveness)

**Media track (user priority: real MP3 / MP4 that "just work" like other OSes)**
- [x] Stage 0a: seek/size syscalls, large files, fast storage writes
- [ ] Stage 0b: **streaming audio**: circular multi-descriptor AC'97 ring with refill, `sys_audio_write`-style API, A/V master clock; fix AC'97 ignoring sample rate/channels (resample to 48 kHz in userspace, fixed-point only); backend interface so Intel HDA (testable in QEMU: `-device intel-hda -device hda-duplex`) can replace AC'97 for real hardware
- [ ] Stage 1: MP3 (Layer III) integer decoder, ID3/Xing/VBR; verify in a host harness against `ffmpeg` output (SNR) for CBR/VBR/mono/32/44.1/48 kHz
- [ ] Stage 2: MP4 demuxer (`moov` at end handled via seek) + AAC-LC audio
- [ ] Stage 3: H.264 Constrained Baseline 480p/30 integer decoder (+ later Main/High); measure ticks per frame early; drop frames rather than slip audio
- Today: images PNG/JPEG, WAV, custom `.ymjp` Motion-JPEG video (reads whole file into a 16 MB static buffer). Everything must stay integer/fixed-point (no FPU/SSE in userspace builds).

---

## 6. Build / boot / test cheat-sheet

```bash
cd /home/yousuf/codes/YouOS
bash tests/host/run_host_tests.sh      # optional, ~1 min; must end ALL HOST TESTS PASSED
bash rebuild.sh                         # user build: checks tree version, link pre-check, userspace, initrd, kernel, ISO, verifies desktop is embedded
# then the QEMU command from §3
python3 tools/ycfs.py fsck disk.img     # after each session, QEMU closed; must end OK
```
Restore the test files without touching anything existing (QEMU closed):
```bash
cat > /tmp/restore_test_files.sh << 'EOF'
#!/bin/bash
cd /home/yousuf/codes/YouOS || exit 1
Y="python3 tools/ycfs.py"; IMG=disk.img
DING="/home/yousuf/codes/os/assets/sounds/ding.wav"
PNG="/home/yousuf/Pictures/Screenshots/Screenshot from 2026-01-13 10-28-37.png"
JPG="/home/yousuf/Pictures/Screenshots/test_for_youos.jpg"
YMJP="/home/yousuf/Pictures/Screenshots/test_for_youos.ymjp"
exists() { $Y ls "$IMG" "$1" >/dev/null 2>&1; }
put_if_missing() { [ -f "$1" ] || { echo "  skip $2"; return; }; exists "$2" && { echo "  keep $2"; return; }; $Y put "$IMG" "$1" "$2" >/dev/null && echo "  added $2"; }
[ -f /tmp/big.bin ] || head -c 20000000 /dev/urandom > /tmp/big.bin
$Y mkdir "$IMG" /test -p
put_if_missing /tmp/big.bin /test/big.bin; put_if_missing "$DING" /test/ding.wav; put_if_missing "$PNG" /test/test.png
put_if_missing "$JPG" /test/test.jpg; put_if_missing "$YMJP" /test/test.ymjp
$Y ls "$IMG" /test; $Y fsck "$IMG"
EOF
bash /tmp/restore_test_files.sh
```
Backups the user made (safe to delete once happy): `disk.img.bak2/3/4`, `*.ycfs-backup-*.tar`, `build/youos_prev.iso`, `build/youos_v3.iso`.

---

## 7. NEW FEATURE (separate checklist item): system-wide text clipboard

**Goal:** select/copy/cut text anywhere in YouOS and paste it anywhere in YouOS — Notepad, the terminal(s), log views, the notification center, text fields (rename box, search box, calculator display where sensible), file-manager dialogs. It is separate from the existing *file* clipboard in the file manager (`fm_clip*`), which must keep working unchanged.

**Facts to build on (verify by reading code first):**
* Almost every text surface is drawn by the single desktop process, so a clipboard buffer in `desktop.c` (e.g. 64 KB, plus a "has text" flag) covers them. Terminal windows (`WIN_PTERM`) are separate *processes* whose output the desktop draws; pasting into one means the desktop injects the text as keyboard input to that process (or writes to its input pipe) — find out how `pterm` input is delivered.
* Keyboard input arrives through `sys_keypoll()` (one value per call; `0` = none). **Unknown: whether Ctrl/Shift are reported** (needed for Ctrl+C/X/V, Shift+arrows, Ctrl+A). Check `kernel/drivers/keyboard.c`; the kernel may need to expose modifier state/ctrl-codes.
* Selection needs per-surface support: Notepad (shift+arrows, mouse drag, double/triple click), terminal output (mouse drag over the scrollback), log/notification text (mouse drag or "copy" button/context-menu item), single-line fields.
* Right-click context menus already exist for windows; a "Copy / Paste" context menu on text areas is a natural fit.

**Requirements to confirm with the user at the start:** keyboard shortcuts (Ctrl+C/X/V; terminal convention Ctrl+Shift+C/V since Ctrl+C = interrupt?), whether pasting multi-line text into the terminal should execute lines, size limit, whether passwords fields must never copy, and whether a clipboard history is wanted.
**Testing:** extract clipboard/selection logic into header-only code testable in the host harness (like `fs_tree.h`); render selection highlight states with the `render_transfer.c` technique before booting.

---

## 8. Notepad improvements

The user will list specific Notepad improvements **in the new chat**. Do not guess or pre-implement. Known current behaviour to be aware of: per-window state `np_states[]` (`np` macro = state of `np_current`), full-path load/save (`np.path`), Save As always writes to the root folder, Open dialog lists the root folder only, no selection/undo/find yet. The clipboard (§7) will interact with whatever selection model is added here — design them together.

---

## 9. Suggested agenda for the first session in the new chat

1. Do §0 (push or upload) and re-read §1; clone, confirm HEAD/`VERSION`, run `bash tests/host/run_host_tests.sh`.
2. Ask the user for: first 45 boot lines (DMA line?), the `[xfer]` lines from a hands-off copy, a hands-off screen recording (note `[xfer]` printing may be the flicker — ask them to also try a build without it).
3. Fix the copy problem in the order of §4 (Stage 0 → 1 → 2), delivering each stage as its own uniquely named archive with `rebuild.sh`, host tests, and a mutation check; ask for a recording + serial lines after each.
4. Then the clipboard (§7), then Notepad (§8), then back to the roadmap (§5) — streaming audio next on the media track.
5. Keep commits flowing to GitHub (ask the user to `git push`) so the sandbox can always clone the true state.
