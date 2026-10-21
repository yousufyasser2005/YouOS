#!/usr/bin/env python3
"""Tests for `ycfs.py fsck --repair`: build images with each kind of damage, repair, verify."""
import contextlib, io, os, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import ycfs

Y = [sys.executable, os.path.join(HERE, "..", "..", "tools", "ycfs.py")]
fails = 0
def ok(cond, msg):
    global fails
    print("  %s  %s" % ("ok  " if cond else "FAIL", msg))
    if not cond: fails += 1

def fresh(tmp, name):
    path = os.path.join(tmp, name)
    with open(path, "wb") as f: f.truncate(64 * 1024 * 1024)
    with contextlib.redirect_stdout(io.StringIO()):
        ycfs.mkfs(path, 256 * 1024 * 1024)
    return path

def fsck_clean(path):
    img = ycfs.Image(path)
    with contextlib.redirect_stdout(io.StringIO()):
        n = ycfs.fsck(img, verbose=False)
    img.close()
    return n == 0

with tempfile.TemporaryDirectory() as tmp:
    # 1. the old kernel bug: a folder deleted without deleting what was inside it
    p = fresh(tmp, "orphans.img")
    img = ycfs.Image(p, write=True)
    img.mkdir("/d1/sub", parents=True)
    img.put_bytes("/d1/a.txt", b"precious note inside the folder\n")
    data = os.urandom(100000)
    img.put_bytes("/d1/sub/b.bin", data)
    img.put_bytes("/keep.txt", b"unrelated\n")
    d1 = img.resolve("/d1"); ino = img.read_inode(d1)
    img.remove_dirent(ycfs.ROOT_INODE, "d1")
    img.free_inode_data(ino); img.write_inode(d1, ycfs.Inode(0)); img.free_inode(d1)
    img.close()
    ok(not fsck_clean(p), "damaged image is detected by fsck")
    r = subprocess.run(Y + ["fsck", p, "--repair"], capture_output=True, text=True)
    ok(r.returncode == 0, "fsck --repair succeeds")
    ok(fsck_clean(p), "image is clean after repair")
    img = ycfs.Image(p)
    names = [e[1] for e in img.dir_entries(img.read_inode(img.resolve("/lost+found")))]
    ok(len(names) == 2, "two orphans were linked into /lost+found (%s)" % names)
    found = {}
    for path, _n, i in img.walk(img.resolve("/lost+found"), "/lost+found"):
        if i.mode == ycfs.TYPE_FILE: found[path] = img.read_all(i)
    ok(any(v == b"precious note inside the folder\n" for v in found.values()), "the orphaned note's text was recovered")
    ok(any(v == data for v in found.values()), "the orphaned 100KB file was recovered byte-for-byte")
    ok(img.read_all(img.read_inode(img.resolve("/keep.txt"))) == b"unrelated\n", "unrelated file untouched")
    img.close()

    # 2. a leaked block (marked used, nothing uses it) and wrong counters
    p = fresh(tmp, "leak.img")
    img = ycfs.Image(p, write=True)
    img.put_bytes("/f.txt", b"hello")
    b = img.alloc_block()                                  # allocated and then forgotten
    img.sb["free_blocks"] += 7; img.sb["free_inodes"] -= 3 # lie in the counters
    img.sb_dirty = True; img.close()
    ok(not fsck_clean(p), "leak + bad counters detected")
    subprocess.run(Y + ["fsck", p, "--repair"], capture_output=True, text=True)
    ok(fsck_clean(p), "leaked block freed and counters corrected")

    # 3. the dangerous one: a block in use but marked free
    p = fresh(tmp, "danger.img")
    img = ycfs.Image(p, write=True)
    img.put_bytes("/f.bin", os.urandom(20000))
    blk = img.read_inode(img.resolve("/f.bin")).direct[0]
    img._clr(img.bbm, blk); img.sb["free_blocks"] += 1; img.bbm_dirty = img.sb_dirty = True
    img.close()
    ok(not fsck_clean(p), "in-use-but-marked-free block detected")
    subprocess.run(Y + ["fsck", p, "--repair"], capture_output=True, text=True)
    ok(fsck_clean(p), "block re-marked as in use")

    # 4. repair refuses to touch an image with an unfinished journal transaction
    p = fresh(tmp, "journal.img")
    img = ycfs.Image(p, write=True)
    import struct
    jb = struct.pack("<I", ycfs.JOURNAL_MAGIC) + b"\0" * (ycfs.BLOCK - 4)
    img.write_blk(img.sb["journal_start_block"], jb); img.close()
    r = subprocess.run(Y + ["fsck", p, "--repair"], capture_output=True, text=True)
    ok(r.returncode != 0 and "journal" in r.stderr.lower(), "repair refuses an unclean journal")

    # 5. a healthy image: nothing to do
    p = fresh(tmp, "ok.img")
    r = subprocess.run(Y + ["fsck", p, "--repair"], capture_output=True, text=True)
    ok(r.returncode == 0 and "nothing to repair" in r.stdout, "healthy image: nothing to repair")

print("repair tests: %s" % ("ALL PASSED" if not fails else "%d FAILED" % fails))
sys.exit(1 if fails else 0)
