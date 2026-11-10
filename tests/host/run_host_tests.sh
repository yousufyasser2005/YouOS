#!/usr/bin/env bash
# Host-side regression tests for the storage stack (no QEMU needed).
#   - tools/ycfs.py        (format / put / get / rm / fsck / migrate)
#   - kernel/fs/ycfs.c     (the REAL kernel source, compiled for the host with a file-backed fake disk)
#   - kernel/drivers/ata.c (the REAL driver against a spec-modelled mock IDE controller; LBA28 vs LBA48)
# Run from the repository root:   bash tests/host/run_host_tests.sh
# Needs: gcc (host compiler), python3.  Uses ~200MB of temp space, mostly sparse.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); cd "$ROOT"
W=$(mktemp -d /tmp/youos_hosttest.XXXXXX); trap 'rm -rf "$W"' EXIT
Y="python3 $ROOT/tools/ycfs.py"; fails=0
ok()   { echo "  ok    $1"; }
bad()  { echo "  FAIL  $1"; fails=$((fails+1)); }
check(){ if eval "$2" >/dev/null 2>&1; then ok "$1"; else bad "$1"; fi; }

echo "== build host harnesses"
gcc -g -w -fsanitize=address,undefined -I include -o "$W/kh" tests/host/ycfs_kernel_harness.c kernel/fs/ycfs.c || { echo "harness build failed"; exit 1; }
python3 "$ROOT/tests/host/gen_ata_mock.py" "$ROOT" "$W" || { echo "ata mock generation failed"; exit 1; }
cp tests/host/ata_mock_test.c tests/host/vga_stub.c "$W/"
gcc -g -w -I include -I "$W" -o "$W/ata_test" "$W/ata_mock_test.c" "$W/vga_stub.c" || { echo "ata test build failed"; exit 1; }

echo "== ATA driver: LBA28 below 2^28 sectors, LBA48 above (mock controller)"
"$W/ata_test" > "$W/ata.out" 2>&1; rc=$?; grep -c '^ok' "$W/ata.out" | sed 's/^/  checks passed: /'
[ $rc -eq 0 ] && ok "ATA driver" || { cat "$W/ata.out"; bad "ATA driver"; }

echo "== kernel mutex: mutual exclusion, recursion, owner death, deferred kill (real sync.c, simulated scheduler)"
gcc -g -O0 -fsanitize=undefined -DSYNC_HOST_TEST -I include -o "$W/sync_test" tests/host/sync_test.c kernel/proc/sync.c || { echo "sync test build failed"; exit 1; }
"$W/sync_test" > "$W/sync.out" 2>&1; rc=$?; grep -c '^  ok' "$W/sync.out" | sed 's/^/  checks passed: /'
[ $rc -eq 0 ] && ok "kernel mutex" || { cat "$W/sync.out"; bad "kernel mutex"; }

echo "== kernel link sanity (every referenced symbol is defined somewhere)"
bash "$ROOT/tests/host/link_check.sh" > "$W/link.out" 2>&1 && ok "$(tail -1 "$W/link.out")" || { cat "$W/link.out"; bad "kernel link check"; }

echo "== ycfs.py: migrate a v1 image (the old 16MB layout), keeping files and the FAT16 area"
cd "$W"; head -c 1048576 /dev/urandom > fat.bin; truncate -s 64M disk.img; dd if=fat.bin of=disk.img conv=notrunc 2>/dev/null
head -c 67108864 disk.img | sha256sum > fat.sha
# build a genuine v1 image with a tiny inline copy of the old format writer
python3 - << 'PYEOF'
import struct
B=4096; S=131072*512
f=open("disk.img","r+b"); f.truncate(S+16*1024*1024)
def blk(n,d): f.seek(S+n*B); f.write(d.ljust(B,b"\0"))
sb=struct.pack("<15I",0x59434653,1,B,4096,1024,4096-164,1024-3,1,2,3,32,163-0,1,35,128)
# layout: sb0, ibm1, bbm2, itable 3..34, journal 35..162, data 163..
sb=struct.pack("<15I",0x59434653,1,B,4096,1024,4096-165,1024-3,1,2,3,32,163,1,35,128)
blk(0,sb); blk(1,bytes([0b111])); 
bm=bytearray(B)
for i in range(165): bm[i>>3]|=1<<(i&7)
blk(2,bytes(bm))
def inode(mode,size,used,direct,uid=0,gid=0,perm=0o644):
    return struct.pack("<IQII12IIQIIH",mode,size,1,used,*(direct+[0]*(12-len(direct))),0,0,uid,gid,perm).ljust(128,b"\0")
hello=b"hello v1\n"
ents=struct.pack("<IHBB56s",2,64,5,1,b"a.txt".ljust(56,b"\0"))+struct.pack("<IHBB56s",3,64,8,1,b"auth.dat".ljust(56,b"\0"))
blk(3,b"\0"*128+inode(2,len(ents),1,[163])+inode(1,len(hello),1,[164])+inode(1,11,1,[165],uid=1,gid=1,perm=0o600))
blk(163,ents); blk(164,hello); blk(165,b"user1:hash\n")
bm=bytearray(bm)
for i in (165,): bm[i>>3]|=1<<(i&7)
blk(2,bytes(bm)); blk(1,bytes([0b1111]))
sb=struct.pack("<15I",0x59434653,1,B,4096,1024,4096-166,1024-4,1,2,3,32,163,1,35,128); blk(0,sb)
PYEOF
$Y fsck disk.img | tail -1 | grep -q OK && ok "hand-built v1 image passes fsck" || bad "v1 fsck"
$Y migrate disk.img --size 2G > mig.out 2>&1 && ok "migrate v1 -> v2 (2G)" || { cat mig.out; bad "migrate"; }
$Y get disk.img /auth.dat auth.out >/dev/null 2>&1 && [ "$(cat auth.out)" = "user1:hash" ] && ok "account file content survived" || bad "account file content"
$Y ls disk.img | grep auth.dat | grep -q -- "-rw-------    1    1" && ok "owner uid/gid and 0600 survived" || bad "ownership"
head -c 67108864 disk.img | sha256sum | cmp -s - fat.sha && ok "FAT16 area byte-identical" || bad "FAT16 area changed"

echo "== file-size boundaries (direct / indirect / double-indirect), tool <-> real kernel code"
mkdir -p src out
for n in 0 1 4095 4096 4097 49152 49153 4243455 4243456 4243457 8486912 20971521; do head -c $n /dev/urandom > src/f_$n.bin; done
for f in src/*.bin; do $Y put disk.img "$f" /$(basename "$f") >/dev/null; done
$Y fsck disk.img | tail -1 | grep -q OK && ok "fsck after tool wrote all sizes" || bad "fsck after puts"
for f in src/*.bin; do b=$(basename "$f"); "$W/kh" disk.img read "$b" out/k_$b >/dev/null 2>&1 && cmp -s "$f" out/k_$b && ok "kernel reads tool-written $b" || bad "kernel reads $b"; done
for f in src/*.bin; do b=$(basename "$f"); $Y rm disk.img /$b; done
for f in src/*.bin; do b=$(basename "$f" .bin); "$W/kh" disk.img write $b.k "$f" >/dev/null 2>&1 || bad "kernel write $b"; done
$Y fsck disk.img | tail -1 | grep -q OK && ok "fsck after KERNEL wrote all sizes" || bad "fsck after kernel writes"
for f in src/*.bin; do b=$(basename "$f" .bin); $Y get disk.img /$b.k out/p_$b >/dev/null 2>&1 && cmp -s "$f" out/p_$b && ok "tool reads kernel-written $b" || bad "tool reads $b"; done
for f in src/*.bin; do b=$(basename "$f" .bin); "$W/kh" disk.img unlink $b.k >/dev/null 2>&1 || bad "kernel unlink $b"; done
$Y fsck disk.img | tail -1 | grep -q OK && ok "fsck after kernel unlinks (no leaks, counters exact)" || bad "fsck after unlinks"
"$W/kh" disk.img journaltest 2>&1 | grep -q PASS && ok "journal self-test" || bad "journal self-test"

echo "== read batching: contiguous blocks need few disk commands; interleaved (fragmented) files still read exactly"
head -c 20971520 /dev/urandom > src/rb20.bin
"$W/kh" disk.img write rb20 src/rb20.bin >/dev/null 2>&1 || bad "write 20MB for read-count test"
RC=$("$W/kh" disk.img readcount rb20 2>/dev/null | grep -o "in [0-9]* read" | grep -o "[0-9]*")
[ -n "$RC" ] && [ "$RC" -lt 400 ] && ok "20MB contiguous file read with $RC disk commands (4KB-per-command would need 5120+)" || bad "read batching: $RC commands"
"$W/kh" disk.img read rb20 out/rb20.out >/dev/null 2>&1 && cmp -s src/rb20.bin out/rb20.out && ok "batched read returns the exact 20MB" || bad "batched read content"
FR=$("$W/kh" disk.img frag 2>&1 | tee frag.out | grep -o "[0-9]* read commands" | grep -o "^[0-9]*")
grep -q "frag: PASS" frag.out && ok "interleaved files read back exactly (incl. an unaligned range across gaps)" || bad "fragmented read content"
[ -n "$FR" ] && [ "$FR" -gt 150 ] && ok "the test files really were fragmented ($FR read commands for 400 blocks)" || bad "fragmentation test is vacuous ($FR read commands)"
$Y fsck disk.img | tail -1 | grep -q OK && ok "fsck after the read-batching runs" || bad "fsck after read batching"

echo "== 1 TB image: metadata, and the kernel allocating past the 128GB LBA28 limit"
truncate -s 64M big.img; $Y mkfs big.img --size 1T --yes >/dev/null && ok "mkfs 1TB" || bad "mkfs 1TB"
$Y fsck big.img | tail -1 | grep -q OK && ok "fsck 1TB (empty)" || bad "fsck 1TB"
python3 - "$ROOT" << 'PYEOF'
import sys; sys.path.insert(0, sys.argv[1] + "/tools")
import ycfs
img = ycfs.Image("big.img", write=True); img.sb["block_hint"] = (600 * 2**30) // 4096; img.sb_dirty = True; img.close()
PYEOF
head -c 5000001 /dev/urandom > hi.bin
"$W/kh" big.img write hi.k hi.bin > hi.log 2>&1; lba=$(grep -o 'highest LBA [0-9]*' hi.log | grep -o '[0-9]*$')
[ "${lba:-0}" -gt 268435456 ] && ok "kernel wrote beyond sector 2^28 (highest LBA $lba)" || bad "did not reach high LBA"
$Y get big.img /hi.k hi.out >/dev/null 2>&1 && cmp -s hi.bin hi.out && ok "tool reads file stored at ~600GB" || bad "high-LBA readback"
"$W/kh" big.img read hi.k hi.k.out >/dev/null 2>&1 && cmp -s hi.bin hi.k.out && ok "kernel re-reads file stored at ~600GB" || bad "kernel high-LBA readback"
$Y fsck big.img | tail -1 | grep -q OK && ok "fsck 1TB after kernel write" || bad "fsck 1TB after write"

echo "== kernel file semantics: shrinking overwrite, chunked writes, folder safety (one continuous mount)"
cd "$W"; truncate -s 64M ops.img; $Y mkfs ops.img --size 2G --yes >/dev/null
head -c 20000000 /dev/urandom > huge.bin; head -c 3000000 /dev/urandom > mid.bin; head -c 30000 /dev/urandom > small.bin
head -c 5 /dev/urandom > tiny.bin; printf '%0200d' 0 | tr 0 A > long.txt; printf short > short.txt
cat > ops.txt << 'OPS'
save /n.txt long.txt
save /n.txt short.txt
size /n.txt 5
read /n.txt back.txt
save /h.bin huge.bin
save /h.bin small.bin
size /h.bin 30000
read /h.bin hback.bin
save /m.bin mid.bin
save /m.bin tiny.bin
size /m.bin 5
chunkfile /c.bin huge.bin 65536
size /c.bin 20000000
chunkfile /c.bin small.bin 4096
size /c.bin 30000
chunkfile /odd.bin huge.bin 4093
chunkfile /empty.bin short.txt 0
mkdir /d1
save /d1/a.txt short.txt
unlink_must_fail /d1
mkdir /d2
rename_must_fail /d1 /d1/inside
rename_must_fail /d1 /d1
rename /d1 /d2/d1
read /d2/d1/a.txt moved.txt
unlink /d2/d1/a.txt
unlink /d2/d1
unlink /d2
OPS
"$W/kh" ops.img script ops.txt > ops.out 2>&1; grep -q "errors=0" ops.out && ok "kernel operations script (no failures)" || { cat ops.out; bad "kernel operations script"; }
head -c 5 short.txt | cmp -s - back.txt && ok "overwriting a longer file with 5 bytes leaves exactly 5 bytes" || bad "shrink-overwrite content"
head -c 30000 small.bin | cmp -s - <(head -c 30000 hback.bin) && ok "20MB file overwritten by 30KB: content exact" || bad "20MB->30KB content"
cmp -s short.txt moved.txt && ok "moved folder's file intact" || bad "moved folder content"
$Y get ops.img /odd.bin odd.out >/dev/null 2>&1; cmp -s huge.bin odd.out && ok "20MB written in odd 4093-byte chunks: byte-identical" || bad "chunked 20MB"
$Y fsck ops.img | tail -1 | grep -q OK && ok "fsck after all of it (no leaks, no orphans)" || bad "fsck after operations"

echo "== write efficiency (every disk command costs a drive-cache flush, so fewer commands = faster copies)"
truncate -s 64M perf.img; $Y mkfs perf.img --size 1G --yes >/dev/null
printf 'chunkfile /big.bin huge.bin 65536\n' > perf1.txt
w=$("$W/kh" perf.img script perf1.txt 2>&1 | grep -o '[0-9]* writes' | grep -o '^[0-9]*')
[ "${w:-999999}" -lt 2500 ] && ok "20MB copy in 64KB chunks: $w write commands (was 20003 before batching)" || bad "20MB copy used ${w:-?} write commands (limit 2500)"
printf 'chunkfile /big1m.bin huge.bin 1048576\n' > perf3.txt
w=$("$W/kh" perf.img script perf3.txt 2>&1 | grep -o '[0-9]* writes' | grep -o '^[0-9]*')
[ "${w:-999999}" -lt 400 ] && ok "20MB copy in 1MB chunks: $w write commands (8KB chunks cost 12231)" || bad "20MB in 1MB chunks used ${w:-?} write commands (limit 400)"
for i in 1 2 3 4 5 6 7 8 9 10; do echo "chunkfile /s$i.bin tiny.bin 65536"; done > perf2.txt
w=$("$W/kh" perf.img script perf2.txt 2>&1 | grep -o '[0-9]* writes' | grep -o '^[0-9]*')
[ "${w:-999999}" -lt 400 ] && ok "10 small files: $w write commands (was 438)" || bad "10 small files used ${w:-?} write commands (limit 400)"
$Y fsck perf.img | tail -1 | grep -q OK && ok "fsck after the efficiency runs" || bad "fsck after efficiency runs"

echo "== file manager copy / move / delete (user/src/fs_tree.h on the real kernel filesystem)"
gcc -g -w -fsanitize=address,undefined -I "$ROOT/include" -o "$W/ftt" "$ROOT/tests/host/fs_tree_test.c" "$ROOT/kernel/fs/ycfs.c" || { echo "fs_tree test build failed"; exit 1; }
T="python3 $ROOT/tests/host/tree_check.py"; F="$W/ftt"
truncate -s 64M ft.img; $Y mkfs ft.img --size 1G --yes >/dev/null; $T setup ft.img
check2(){ if eval "$2" >/dev/null 2>&1; then ok "$1"; else bad "$1"; fi; }
$F ft.img copy /ycfs/src /ycfs/dest/src 1 >/dev/null 2>&1;      check2 "copy folder tree (nested, empty file, 5MB file) is identical" "$T compare ft.img /src /dest/src"
$F ft.img copy /ycfs/huge.bin /ycfs/dest/huge.bin 0 >/dev/null 2>&1; check2 "copy 20MB file in chunks is identical" "$T compare ft.img /huge.bin /dest/huge.bin"
$F ft.img copy /ycfs/many /ycfs/dest/many 1 >/dev/null 2>&1;    check2 "copy 100-entry folder is identical" "$T compare ft.img /many /dest/many"
check2 "refuses copying a folder into itself" "! $F ft.img copy /ycfs/src /ycfs/src/inside 1"
check2 "...and leaves nothing behind" "$T absent ft.img /src/inside"
check2 "refuses a 300-entry folder instead of silently truncating" "! $F ft.img copy /ycfs/toomany /ycfs/dest/toomany 1"
check2 "...and leaves no half-copy" "$T absent ft.img /dest/toomany"
check2 "refuses a too-deeply-nested copy" "! $F ft.img copy /ycfs/deep1 /ycfs/dest/deep1 1"
check2 "...and leaves no half-copy" "$T absent ft.img /dest/deep1"
$F ft.img move /ycfs/dest/src /ycfs/src_moved >/dev/null 2>&1;  check2 "move (cut+paste) keeps a folder's contents" "$T compare ft.img /src /src_moved"
check2 "old location is gone after a move" "$T absent ft.img /dest/src"
check2 "refuses moving a folder into its own subfolder" "! $F ft.img move /ycfs/src_moved /ycfs/src_moved/d1/x"
check2 "refuses moving a folder onto itself" "! $F ft.img move /ycfs/src_moved /ycfs/src_moved"
check2 "...and the folder is still intact" "$T compare ft.img /src /src_moved"
$F ft.img remove /ycfs/src_moved 1 >/dev/null 2>&1;             check2 "delete non-empty folder tree" "$T absent ft.img /src_moved"
$F ft.img remove /ycfs/toomany 1 >/dev/null 2>&1;               check2 "delete folder with 300 entries" "$T absent ft.img /toomany"
$F ft.img remove /ycfs/deep1 1 >/dev/null 2>&1;                 check2 "delete 7-level-deep tree" "$T absent ft.img /deep1"
$F ft.img remove /ycfs/dest 1 >/dev/null 2>&1;                  check2 "delete tree holding copies of a 20MB file" "$T absent ft.img /dest"
$F ft.img remove /ycfs/many 1 >/dev/null 2>&1; $F ft.img remove /ycfs/huge.bin 0 >/dev/null 2>&1; $F ft.img remove /ycfs/src 1 >/dev/null 2>&1
$Y fsck ft.img | tail -1 | grep -q "OK" && ok "fsck after all copies/moves/deletes (nothing leaked)" || bad "fsck after file-manager operations"

echo "== non-blocking copy job (ft_job_*): bounded steps, progress, cancel, failure cleanup"
truncate -s 64M job.img; $Y mkfs job.img --size 1G --yes >/dev/null; $T setup job.img
jobout=$($F job.img job /ycfs/src /ycfs/dest/src 1 3 2>&1 | grep '^job')
echo "$jobout" | grep -q "job DONE" && ok "folder tree copied by the job engine" || bad "job folder tree: $jobout"
echo "$jobout" | grep -q "pct_regressed=0" && ok "progress never goes backwards" || bad "progress regressed"
check2 "job copy is identical to the source" "$T compare job.img /src /dest/src"
steps=$(echo "$jobout" | grep -o 'steps=[0-9]*' | grep -o '[0-9]*'); [ "${steps:-0}" -gt 10 ] && ok "work was split into $steps small steps (not one blocking call)" || bad "job ran in only ${steps:-?} steps"
jobout=$($F job.img job /ycfs/huge.bin /ycfs/dest/huge.bin 0 3 2>&1 | grep '^job')
mx=$(echo "$jobout" | grep -o 'max_bytes_per_step=[0-9]*' | grep -o '[0-9]*$'); [ "${mx:-999999999}" -le 1048576 ] && ok "no step moved more than 1MB (moved at most $mx)" || bad "a step moved $mx bytes"
check2 "20MB file copied by the job is identical" "$T compare job.img /huge.bin /dest/huge.bin"
$F job.img job /ycfs/src /ycfs/dest/cancelled 1 3 12 >/dev/null 2>&1; check2 "cancelled folder copy leaves nothing behind" "$T absent job.img /dest/cancelled"
$F job.img job /ycfs/huge.bin /ycfs/dest/h2.bin 0 3 6 >/dev/null 2>&1;     # chunks double each step, so a 20MB copy is over in ~20 steps: cancel early
 check2 "cancelled 20MB copy removes the partial file" "$T absent job.img /dest/h2.bin"
check2 "300-entry folder refused before anything is created" "! $F job.img job /ycfs/toomany /ycfs/dest/toomany 1 3"
check2 "...nothing created" "$T absent job.img /dest/toomany"
check2 "too-deep folder refused during the scan" "! $F job.img job /ycfs/deep1 /ycfs/dest/deep1 1 3"
check2 "...nothing created" "$T absent job.img /dest/deep1"
check2 "copying a folder into itself refused" "! $F job.img job /ycfs/src /ycfs/src/inside 1 3"
$F job.img job /ycfs/src/empty.bin /ycfs/dest/empty2.bin 0 3 >/dev/null 2>&1; check2 "empty file copies" "$T compare job.img /src/empty.bin /dest/empty2.bin"
$F job.img remove /ycfs/dest 1 >/dev/null 2>&1
$Y fsck job.img | tail -1 | grep -q OK && ok "fsck after every job scenario" || bad "fsck after jobs"
truncate -s 64M full.img; $Y mkfs full.img --size 80M --yes >/dev/null; head -c 9000000 /dev/urandom > nine.bin; $Y put full.img nine.bin /nine.bin >/dev/null
free0=$($Y info full.img | grep 'free  ')
check2 "copy larger than the free space fails" "! $F full.img job /ycfs/nine.bin /ycfs/nine_copy.bin 0 3"
check2 "...partial copy removed" "$T absent full.img /nine_copy.bin"
[ "$($Y info full.img | grep 'free  ')" = "$free0" ] && ok "...free space exactly restored" || bad "free space not restored after failed copy"
$Y fsck full.img | tail -1 | grep -q OK && ok "fsck after disk-full failure" || bad "fsck after disk-full"

echo "== background transfer worker (xfer_proto.h: desktop <-> copyd protocol, with the real worker logic and filesystem)"
truncate -s 64M wk.img; $Y mkfs wk.img --size 1G --yes >/dev/null; $T setup wk.img
wo=$($F wk.img wjob 0 /ycfs/src /ycfs/dest/src 1 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob DONE" && ok "folder tree copied through the worker protocol" || bad "worker folder copy: $wo"
echo "$wo" | grep -q "regress=0" && ok "progress snapshots never go backwards" || bad "worker progress regressed"
echo "$wo" | grep -q "msgs=[1-9]" && ok "the desktop received progress snapshots ($(echo "$wo" | grep -o 'msgs=[0-9]*'))" || bad "no snapshots received"
check2 "worker copy is identical to the source" "$T compare wk.img /src /dest/src"
wo=$($F wk.img wjob 0 /ycfs/huge.bin /ycfs/dest/huge.bin 0 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob DONE" && ok "20MB file copied through the worker" || bad "worker 20MB copy: $wo"
check2 "20MB worker copy is identical" "$T compare wk.img /huge.bin /dest/huge.bin"
wo=$($F wk.img wjob 0 /ycfs/src /ycfs/dest/slow 1 -1 1 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob DONE" && ok "desktop that drains rarely (progress queue overflows): copy still completes" || bad "slow desktop: $wo"
check2 "...and the copy is identical" "$T compare wk.img /src /dest/slow"
wo=$($F wk.img wjob 0 /ycfs/huge.bin /ycfs/dest/h2.bin 0 3 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob CANCELLED" && ok "cancel message stops a running 20MB copy" || bad "cancel: $wo"
check2 "...the worker removed the partial file itself" "$T absent wk.img /dest/h2.bin"
wo=$($F wk.img wjob 0 /ycfs/src /ycfs/dest/c2 1 4 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob CANCELLED" && ok "cancel stops a folder copy" || bad "folder cancel: $wo"
check2 "...and nothing is left behind" "$T absent wk.img /dest/c2"
wo=$($F wk.img wjob 0 /ycfs/src /ycfs/dest/c3 1 0 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob CANCELLED" && ok "cancel sent before the worker started" || bad "early cancel: $wo"
check2 "...nothing created" "$T absent wk.img /dest/c3"
wo=$($F wk.img wjob 0 /ycfs/toomany /ycfs/dest/toomany 1 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob FAILED: .*too many" && ok "300-entry folder refused with the reason delivered to the desktop" || bad "refusal: $wo"
check2 "...and nothing was created" "$T absent wk.img /dest/toomany"
wo=$($F wk.img wjob 0 /ycfs/deep1 /ycfs/dest/deep1 1 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob FAILED" && ok "too-deep folder refused through the worker" || bad "deep refusal: $wo"
wo=$($F wk.img wjob 0 /ycfs/src /ycfs/dest/pre 1 -1 0 1 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob DONE" && ok "stale messages left from an earlier job are cleared, not mistaken for this one" || bad "stale messages: $wo"
check2 "...copy identical" "$T compare wk.img /src /dest/pre"
wo=$($F wk.img wjob 1 /ycfs/dest/pre "" 1 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob DONE" && ok "folder deleted through the worker" || bad "worker delete: $wo"
check2 "...it is gone" "$T absent wk.img /dest/pre"
wo=$($F wk.img wjob 1 /ycfs/dest/slow "" 1 3 2>&1 | grep '^wjob')
echo "$wo" | grep -q "wjob CANCELLED" && ok "delete can be stopped part-way" || bad "delete cancel: $wo"
$F wk.img wjob 1 /ycfs/dest "" 1 >/dev/null 2>&1; check2 "...and the rest can then be deleted" "$T absent wk.img /dest"
$F wk.img wproto 2>&1 | grep -q "wproto OK" && ok "protocol: 219-char paths survive splitting; missing request reported; garbage ignored; full queue cannot lose the final report" || bad "protocol corner cases"
$Y fsck wk.img | tail -1 | grep -q OK && ok "fsck after every worker scenario" || bad "fsck after worker scenarios"

echo "== ycfs.py fsck --repair (orphans -> /lost+found, leaks, bad counters, unclean journal)"
python3 "$ROOT/tests/host/repair_test.py" > repair.out 2>&1; rc=$?
grep -E "^  (ok|FAIL)" repair.out | sed 's/^  ok  /  ok    /' | sed 's/^  FAIL  /  FAIL  /'
[ $rc -eq 0 ] || { fails=$((fails+1)); }

echo "== copy pacing adapts to disk speed (fake clock: 1ms vs 400ms per round trip; hitch limit 250ms)"
$Y mkdir job.img /dest >/dev/null 2>&1
fast=$(FAKE_STEP=1 $F job.img job /ycfs/huge.bin /ycfs/dest/fast.bin 0 3 2>&1 | grep '^job')
echo "$fast" | grep -q "job DONE" && ok "copy on a fast simulated disk" || bad "fast-disk copy: $fast"
cl=$(echo "$fast" | grep -o 'chunk_len=[0-9]*' | grep -o '[0-9]*$'); [ "${cl:-0}" -ge 1000000 ] && ok "fast disk: round trips grow to $cl bytes" || bad "fast disk chunk_len=$cl"
check2 "fast-disk copy is identical" "$T compare job.img /huge.bin /dest/fast.bin"
slow=$(FAKE_STEP=400 $F job.img job /ycfs/huge.bin /ycfs/dest/slow.bin 0 3 2>&1 | grep '^job')
echo "$slow" | grep -q "job DONE" && ok "copy on a very slow simulated disk" || bad "slow-disk copy: $slow"
cl=$(echo "$slow" | grep -o 'chunk_len=[0-9]*' | grep -o '[0-9]*$'); [ "${cl:-999999}" -le 16384 ] && ok "slow disk: round trips shrink to $cl bytes" || bad "slow disk chunk_len=$cl"
mx=$(echo "$slow" | grep -o 'max_bytes_per_step=[0-9]*' | grep -o '[0-9]*$'); [ "${mx:-999999}" -le 16384 ] && ok "slow disk: no step moved more than $mx bytes (UI stays responsive)" || bad "slow disk step moved $mx"
check2 "slow-disk copy is identical" "$T compare job.img /huge.bin /dest/slow.bin"
$F job.img remove /ycfs/dest 1 >/dev/null 2>&1

echo "== non-blocking delete job (ft_job_start_delete): progress, huge folders, cancel, no hang"
$Y mkdir job.img /dest >/dev/null 2>&1
$F job.img copy /ycfs/src /ycfs/dest/src 1 >/dev/null 2>&1; $F job.img copy /ycfs/huge.bin /ycfs/dest/huge.bin 0 >/dev/null 2>&1; $F job.img copy /ycfs/many /ycfs/dest/many 1 >/dev/null 2>&1
dj=$($F job.img jobdel /ycfs/dest 1 3 2>&1 | grep '^deljob')
echo "$dj" | grep -q "deljob DONE" && ok "folder tree (with a 20MB file and 100 entries) deleted by the job" || bad "delete job: $dj"
echo "$dj" | grep -q "pct_regressed=0" && ok "delete progress never goes backwards" || bad "delete progress regressed"
echo "$dj" | grep -q "known=1" && ok "item count known up front (real percentage)" || bad "delete total not known"
st=$(echo "$dj" | grep -o 'steps=[0-9]*' | grep -o '[0-9]*'); [ "${st:-0}" -gt 20 ] && ok "delete split into $st small steps" || bad "delete ran in ${st:-?} steps"
check2 "deleted tree is gone" "$T absent job.img /dest"
dj=$($F job.img jobdel /ycfs/toomany 1 3 2>&1 | grep '^deljob'); echo "$dj" | grep -q "deljob DONE" && ok "300-entry folder deleted (too big to count: indeterminate progress)" || bad "300-entry delete: $dj"
echo "$dj" | grep -q "known=0" && ok "...reported as not countable" || bad "300-entry delete claimed a known total"
check2 "...and it is gone" "$T absent job.img /toomany"
dj=$($F job.img jobdel /ycfs/deep1 1 3 2>&1 | grep '^deljob'); echo "$dj" | grep -q "deljob DONE" && ok "7-level-deep tree deleted" || bad "deep delete: $dj"
$F job.img copy /ycfs/many /ycfs/many2 1 >/dev/null 2>&1
$F job.img jobdel /ycfs/many2 1 3 20 >/dev/null 2>&1; $Y fsck job.img | tail -1 | grep -q OK && ok "cancelling a delete mid-way leaves a consistent filesystem" || bad "fsck after cancelled delete"
check2 "...and the not-yet-deleted part is still there" "$T exists job.img /many2"
$F job.img jobdel /ycfs/many2 1 3 >/dev/null 2>&1; check2 "finishing the delete afterwards works" "$T absent job.img /many2"
check2 "deleting a nonexistent item fails cleanly" "! $F job.img jobdel /ycfs/nope.bin 0 3"
$F job.img jobdel /ycfs/many 1 3 >/dev/null 2>&1; $F job.img jobdel /ycfs/src 1 3 >/dev/null 2>&1; $F job.img jobdel /ycfs/huge.bin 0 3 >/dev/null 2>&1
$Y fsck job.img | tail -1 | grep -q OK && ok "fsck after every delete scenario" || bad "fsck after deletes"

echo "== desktop window slots (real desktop.c with stubbed syscalls)"
DW="$W/deskhost"; mkdir -p "$DW/user/lib" "$DW/user/src"; cp "$ROOT"/user/src/*.h "$ROOT"/user/src/desktop.c "$DW/user/src/"
python3 - "$ROOT" "$DW" << 'PY'
import sys
root, w = sys.argv[1:3]
s = open(root + "/user/lib/syscall.h").read()
i = s.index("static inline uint64_t _sc("); j = s.index("}\n", s.index("return r;", i)) + 2
s = s[:i] + ("uint64_t host_sc(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e);\n"
             "static inline uint64_t _sc(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e)"
             "{return host_sc(n,a,b,c,d,e);}\n") + s[j:]
open(w + "/user/lib/syscall.h", "w").write(s)
PY
cp "$ROOT/tests/host/window_slots_test.c" "$DW/"
gcc -g -w -O0 -I "$DW" -DDESKTOP_SRC='"user/src/desktop.c"' -o "$DW/slots" "$DW/window_slots_test.c" && "$DW/slots" > "$DW/slots.out" 2>&1; rc=$?
grep -E '^(ok|FAIL)' "$DW/slots.out" | sed 's/^ok: /  ok    /; s/^FAIL/  FAIL/'
[ $rc -eq 0 ] || bad "desktop window-slot test"

echo
[ $fails -eq 0 ] && echo "ALL HOST TESTS PASSED" || echo "$fails TEST(S) FAILED"
exit $fails
