#!/usr/bin/env python3
"""Helpers for fs_tree_test: build a test tree on an image, compare two subtrees."""
import os, sys, hashlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import ycfs

def setup(path):
    img = ycfs.Image(path, write=True)
    rnd = __import__("random").Random(11)
    img.mkdir("/src/d1/d2/d3", parents=True)
    img.mkdir("/dest")
    img.put_bytes("/src/a.txt", b"hello tree\n")
    img.put_bytes("/src/empty.bin", b"")
    img.put_bytes("/src/big.bin", rnd.randbytes(5_000_003))          # crosses the indirect boundary
    img.put_bytes("/src/d1/b.bin", rnd.randbytes(70_000))
    img.put_bytes("/src/d1/d2/c.bin", rnd.randbytes(4096))
    img.put_bytes("/src/d1/d2/d3/deep.txt", b"deep\n")
    img.mkdir("/many")                                                  # 100 entries: fine
    for i in range(100): img.put_bytes("/many/f%03d" % i, b"x%d" % i)
    img.mkdir("/toomany")                                               # 300 entries: must be refused, not truncated
    for i in range(300): img.put_bytes("/toomany/f%03d" % i, b"y")
    img.mkdir("/deep1/2/3/4/5/6/7", parents=True)                       # deeper than the recursion limit
    img.put_bytes("/deep1/2/3/4/5/6/7/x", b"z")
    img.put_bytes("/huge.bin", rnd.randbytes(20_000_000))               # double-indirect file
    img.close()

def snapshot(img, base):
    num = img.resolve(base)
    if num is None: return None
    out = {}
    top = img.read_inode(num)
    if top.mode != ycfs.TYPE_DIR:           # a single file: compare its content
        return {"": ("f", top.size, hashlib.sha256(img.read_all(top)).hexdigest())}
    for p, _n, ino in img.walk(num, ""):
        out[p] = ("d",) if ino.mode == ycfs.TYPE_DIR else ("f", ino.size, hashlib.sha256(img.read_all(ino)).hexdigest())
    return out

def compare(path, a, b):
    img = ycfs.Image(path)
    sa, sb = snapshot(img, a), snapshot(img, b)
    img.close()
    if sa is None or sb is None: print("MISSING", a if sa is None else b); return 1
    if sa != sb:
        print("DIFFERENT: %d vs %d entries; only-in-a=%s only-in-b=%s" % (len(sa), len(sb), sorted(set(sa)-set(sb))[:3], sorted(set(sb)-set(sa))[:3])); return 1
    print("identical: %d entries" % len(sa)); return 0

def exists(path, p):
    img = ycfs.Image(path); r = img.resolve(p) is not None; img.close(); return 0 if r else 1

if __name__ == "__main__":
    c = sys.argv[1]
    if c == "setup": setup(sys.argv[2])
    elif c == "compare": sys.exit(compare(sys.argv[2], sys.argv[3], sys.argv[4]))
    elif c == "exists": sys.exit(exists(sys.argv[2], sys.argv[3]))
    elif c == "absent": sys.exit(1 - exists(sys.argv[2], sys.argv[3]))   # exit 0 when the path does NOT exist
