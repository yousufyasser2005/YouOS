#!/usr/bin/env bash
# Full YouOS rebuild: userspace -> initrd -> kernel -> ISO, checking every stage.
#
#   cd /home/yousuf/codes/YouOS && bash rebuild.sh
#
# It stops at the FIRST failure and says which stage failed. At the end it
# proves that the desktop you just built is the one inside the kernel, because
# the initrd is compiled into youos.elf: a rebuilt desktop and initrd.img do
# nothing unless the kernel was rebuilt and copied into iso/boot too.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"
[ -f tools/mkinitrd.py ] && [ -d user ] && [ -d build ] || { echo "run this from the YouOS repository folder"; exit 1; }

say()  { printf '\n=== %s\n' "$*"; }
fail() { printf '\n*** FAILED at: %s\n' "$*" >&2; exit 1; }

PROGS="hello cat shell fbtest desktop mpy crashtest exectest spawntest iotest seektest"

say "your source tree"
[ -f VERSION ] && echo "  version file: $(cat VERSION)" || echo "  (no VERSION file: this tree predates v3)"

say "which patches are in your source tree"
chk() { if grep -q -- "$2" "$3" 2>/dev/null; then printf '  [x] %s\n' "$1"; else printf '  [ ] %s   <-- not applied\n' "$1"; fi; }
chk "rtc (real clock)"                 "SYS_RTC"                     include/kernel/syscall.h
chk "seek (fseek/fsize)"               "SYS_FSEEK"                   include/kernel/syscall.h
chk "ycfs v2 (big files/disks)"        "dindirect"                   include/kernel/ycfs.h
chk "fileops (chunked write syscall)"  "SYS_SAVECHUNK"               include/kernel/syscall.h
chk "transfer window"                  "xfer_pump"                   user/src/desktop.c
chk "window slots fix (wm_close)"      "wm_close"                    user/src/desktop.c
chk "delete job"                       "ft_job_start_delete"         user/src/fs_tree.h
chk "big chunks (1MB copy chunks)"     "define FT_CHUNK       1048576" user/src/fs_tree.h
chk "build stamp"                      "YOUOS_BUILD"                 user/src/desktop.c
chk "ATA WRITE MULTIPLE (fast writes)" "ata_write_multi"             kernel/drivers/ata.c

# This script is only meant for a tree that has everything above. If the newest feature is
# missing, the tree is out of date (usually: an older archive was extracted) -- stop NOW
# instead of building something that silently lacks it.
if ! grep -q -- "ata_write_multi" kernel/drivers/ata.c 2>/dev/null; then
  echo
  echo "*** Your source tree is OLDER than this script expects (no WRITE MULTIPLE in kernel/drivers/ata.c)."
  echo "    Extract the NEWEST archive over the repository first, then run this again:"
  echo "        ls -la ~/Downloads/youos_tree_v*.tar     # pick the highest version"
  echo "        tar -xf ~/Downloads/youos_tree_v3.tar"
  echo "    (a browser may have saved the new download as 'name (1).tar' next to an old one with the same name)"
  exit 1
fi

say "1/6  userspace (make clean && make)"
( cd user && make clean >/dev/null && make 2>&1 | tail -3 ) || fail "userspace build"
for p in $PROGS; do [ -s "user/bin/$p" ] || fail "userspace build: user/bin/$p was not produced"; done
echo "  built: $PROGS"

say "2/6  initrd"
args=""; for p in $PROGS; do args="$args user/bin/$p"; done
python3 tools/mkinitrd.py iso/boot/initrd.img $args || fail "mkinitrd"
touch kernel/fs/initrd_blob.asm        # ninja does not track the initrd file itself

say "3/6  kernel (ninja)"
rm -f build/kernel/youos.elf           # so a stale one can never be mistaken for a fresh one
( cd build && ninja -t clean >/dev/null && ninja ) 2>&1 | tail -25 || true
[ -s build/kernel/youos.elf ] || fail "kernel build: build/kernel/youos.elf was NOT produced -- read the ninja output above for the first error"
echo "  kernel built: $(stat -c '%y' build/kernel/youos.elf | cut -c1-19)"

say "4/6  copy kernel into the ISO tree"
cp build/kernel/youos.elf iso/boot/youos.elf || fail "copy kernel"

say "5/6  ISO"
grub-mkrescue -o build/youos.iso iso 2>&1 | tail -2
[ -s build/youos.iso ] || fail "grub-mkrescue: build/youos.iso was not produced"

say "6/6  verify: is the desktop you just built really inside the kernel?"
python3 - << 'PY' || fail "verification"
import re, sys
desk = open("user/bin/desktop", "rb").read()
kern = open("iso/boot/youos.elf", "rb").read()
if desk in kern:
    print("  OK: the freshly built desktop (%d bytes) is embedded in iso/boot/youos.elf" % len(desk))
else:
    print("  *** the desktop binary you just built is NOT inside iso/boot/youos.elf"); sys.exit(1)
m = re.search(rb"\[desktop\] build ([A-Za-z]{3} +\d+ \d{4} \d\d:\d\d:\d\d)", desk)
print("  build stamp inside it:  %s" % (m.group(1).decode() if m else "(none -- the build-stamp patch is not applied)"))
PY
echo "  time now:               $(date '+%b %e %Y %H:%M:%S')"
echo
echo "Done. Boot it, and look for  '[desktop] build <that stamp>'  in the QEMU terminal."
