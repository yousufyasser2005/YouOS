#!/usr/bin/env python3
"""
ycfs.py -- host-side tool for YCFS images (the /ycfs filesystem inside
disk.img).  Adds, extracts and removes files WITHOUT reformatting, so
accounts, settings and everything else already on the image survive.

    ycfs.py info    IMG
    ycfs.py ls      IMG [PATH] [-R]
    ycfs.py put     IMG SRC [SRC...] DEST      copy host files in
    ycfs.py get     IMG PATH [DEST]            copy a file out
    ycfs.py mkdir   IMG PATH [-p]
    ycfs.py rm      IMG PATH [-r]
    ycfs.py fsck    IMG                        consistency check
    ycfs.py mkfs    IMG [--size 4G]            FORMAT (erases /ycfs!)
    ycfs.py export  IMG OUT.tar                everything -> tar archive
    ycfs.py import  IMG IN.tar                 add a tar archive's files
    ycfs.py migrate IMG --size 4G              reformat to a bigger/newer
                                               layout, keeping all files

IMPORTANT: shut QEMU down before touching the image -- writing to a disk
image a running VM also has open corrupts it.  The tool refuses to write
if the journal shows an unfinished transaction (boot the OS once and shut
it down cleanly; the kernel replays and clears it).

Format (see include/kernel/ycfs.h): 4KB blocks starting 64MB into the
disk, running to the end of the image.  v2 adds multi-block bitmaps (any
disk size up to 16TB), allocation hints and a double-indirect block per
inode (files up to ~4GB).  v1 images (the old fixed 16MB layout) are read
fine; `migrate` upgrades them in place without losing files.
"""
import argparse
import math
import os
import re
import struct
import sys
import tarfile
import time

BLOCK = 4096
START_LBA = 131072
START = START_LBA * 512                 # 64MB into the image
INODE_SIZE = 128
DIRENT_SIZE = 64
DIRECT = 12
PPB = BLOCK // 4                        # pointers per pointer block
BITS = BLOCK * 8                        # bits per bitmap block
INODES_PER_BLOCK = BLOCK // INODE_SIZE
MAGIC = 0x59434653
JOURNAL_MAGIC = 0x594A524E
TYPE_FILE, TYPE_DIR = 1, 2
ROOT_INODE = 1
JOURNAL_BLOCKS = 128
MAX_FILE_BLOCKS = DIRECT + PPB + PPB * PPB
MAX_REGION_BLOCKS = 2 ** 32 - 1

SB_FIELDS = ["magic", "version", "block_size", "total_blocks", "total_inodes",
             "free_blocks", "free_inodes", "inode_bitmap_block",
             "block_bitmap_block", "inode_table_block", "inode_table_blocks",
             "data_start_block", "root_inode", "journal_start_block",
             "journal_num_blocks", "block_bitmap_blocks", "inode_bitmap_blocks",
             "block_hint", "inode_hint"]
SB_FMT = "<%dI" % len(SB_FIELDS)
# mode,size,links,blocks_used,direct[12],indirect,mtime,uid,gid,perm,dindirect
INODE_FMT = "<IQII12IIQIIHI"
DIRENT_FMT = "<IHBB56s"
NOT_FF = re.compile(b"[^\xff]")


class YcfsError(Exception):
    pass


def parse_size(text):
    m = re.fullmatch(r"(\d+(?:\.\d+)?)\s*([kKmMgGtT]?)[bB]?", text.strip())
    if not m:
        raise YcfsError("bad size %r (examples: 4G, 512M, 1T)" % text)
    mult = {"": 1, "k": 1 << 10, "m": 1 << 20, "g": 1 << 30, "t": 1 << 40}
    return int(float(m.group(1)) * mult[m.group(2).lower()])


def human(n):
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024 or unit == "TB":
            return ("%d %s" % (n, unit)) if unit == "B" else ("%.1f %s" % (n, unit))
        n /= 1024.0


class Inode:
    __slots__ = ("mode", "size", "links", "blocks_used", "direct", "indirect",
                 "mtime", "uid", "gid", "perm", "dindirect")

    def __init__(self, mode=0):
        self.mode = mode
        self.size = 0
        self.links = 1 if mode else 0
        self.blocks_used = 0
        self.direct = [0] * DIRECT
        self.indirect = 0
        self.mtime = 0
        self.uid = 0
        self.gid = 0
        self.perm = 0o755 if mode == TYPE_DIR else 0o644
        self.dindirect = 0

    @classmethod
    def unpack(cls, raw):
        v = struct.unpack_from(INODE_FMT, raw)
        i = cls()
        i.mode, i.size, i.links, i.blocks_used = v[0:4]
        i.direct = list(v[4:16])
        i.indirect, i.mtime, i.uid, i.gid, i.perm, i.dindirect = v[16:22]
        return i

    def pack(self):
        data = struct.pack(INODE_FMT, self.mode, self.size, self.links,
                           self.blocks_used, *self.direct, self.indirect,
                           self.mtime, self.uid, self.gid, self.perm,
                           self.dindirect)
        return data + b"\x00" * (INODE_SIZE - len(data))


def geometry(region_bytes, bytes_per_inode=32768):
    """Layout for a YCFS region of `region_bytes` bytes."""
    total_blocks = min(region_bytes // BLOCK, MAX_REGION_BLOCKS)
    total_inodes = max(1024, region_bytes // bytes_per_inode)
    total_inodes = (total_inodes + INODES_PER_BLOCK - 1) // INODES_PER_BLOCK * INODES_PER_BLOCK
    total_inodes = min(total_inodes, 2 ** 31)
    ibm = (total_inodes + BITS - 1) // BITS
    bbm = (total_blocks + BITS - 1) // BITS
    itb = total_inodes // INODES_PER_BLOCK
    g = dict(total_blocks=total_blocks, total_inodes=total_inodes,
             inode_bitmap_blocks=ibm, block_bitmap_blocks=bbm,
             inode_bitmap_block=1, block_bitmap_block=1 + ibm)
    g["inode_table_block"] = g["block_bitmap_block"] + bbm
    g["inode_table_blocks"] = itb
    g["journal_start_block"] = g["inode_table_block"] + itb
    g["journal_num_blocks"] = JOURNAL_BLOCKS
    g["data_start_block"] = g["journal_start_block"] + JOURNAL_BLOCKS
    if g["data_start_block"] + 16 > total_blocks:
        raise YcfsError("disk too small for a YCFS (need more than %s after the 64MB FAT16 area)"
                        % human((g["data_start_block"] + 16) * BLOCK))
    return g


class Image:
    def __init__(self, path, write=False):
        self.path = path
        self.write = write
        try:
            self.f = open(path, "r+b" if write else "rb")
        except OSError as e:
            raise YcfsError("cannot open %s: %s" % (path, e))
        self.f.seek(0, os.SEEK_END)
        if self.f.tell() < START + BLOCK * 64:
            raise YcfsError("%s is too small to contain a YCFS region" % path)
        self._load()

    # ---- raw block io ----
    def read_blk(self, n):
        self.f.seek(START + n * BLOCK)
        d = self.f.read(BLOCK)
        return d + b"\x00" * (BLOCK - len(d))

    def read_blks(self, n, count):
        self.f.seek(START + n * BLOCK)
        d = self.f.read(BLOCK * count)
        return d + b"\x00" * (BLOCK * count - len(d))

    def write_blk(self, n, data):
        assert len(data) == BLOCK
        self.f.seek(START + n * BLOCK)
        self.f.write(data)

    # ---- load / save ----
    def _load(self):
        raw = self.read_blk(0)
        vals = struct.unpack_from(SB_FMT, raw)
        self.sb = dict(zip(SB_FIELDS, vals))
        sb = self.sb
        if sb["magic"] != MAGIC:
            raise YcfsError("no YCFS found in %s (bad magic -- not formatted yet?)" % self.path)
        if sb["version"] not in (1, 2):
            raise YcfsError("unsupported YCFS version %d" % sb["version"])
        if sb["block_size"] != BLOCK:
            raise YcfsError("unsupported block size %d" % sb["block_size"])
        sb["block_bitmap_blocks"] = sb["block_bitmap_blocks"] or 1
        sb["inode_bitmap_blocks"] = sb["inode_bitmap_blocks"] or 1
        self.bbm = bytearray(self.read_blks(sb["block_bitmap_block"], sb["block_bitmap_blocks"]))
        self.ibm = bytearray(self.read_blks(sb["inode_bitmap_block"], sb["inode_bitmap_blocks"]))
        self.bbm_dirty = False
        self.ibm_dirty = False
        self.sb_dirty = False

    def journal_dirty(self):
        sb = self.sb
        if not sb["journal_num_blocks"]:
            return False
        data = self.read_blks(sb["journal_start_block"], sb["journal_num_blocks"])
        for i in range(0, len(data), BLOCK):
            if struct.unpack_from("<I", data, i)[0] == JOURNAL_MAGIC:
                return True
        return False

    def require_clean_for_write(self, force=False):
        if self.journal_dirty() and not force:
            raise YcfsError(
                "the image's journal has an unfinished transaction (the OS was not shut down "
                "cleanly). Boot it once and shut down normally so the kernel replays it, "
                "or pass --force to ignore (may lose the last operation).")

    def flush(self):
        if not self.write:
            return
        sb = self.sb
        if self.bbm_dirty:
            self.f.seek(START + sb["block_bitmap_block"] * BLOCK)
            self.f.write(bytes(self.bbm))
        if self.ibm_dirty:
            self.f.seek(START + sb["inode_bitmap_block"] * BLOCK)
            self.f.write(bytes(self.ibm))
        if self.sb_dirty or self.bbm_dirty or self.ibm_dirty:
            vals = [sb[k] for k in SB_FIELDS]
            raw = struct.pack(SB_FMT, *vals)
            self.write_blk(0, raw + b"\x00" * (BLOCK - len(raw)))
        self.bbm_dirty = self.ibm_dirty = self.sb_dirty = False
        self.f.flush()
        os.fsync(self.f.fileno())

    def close(self):
        try:
            self.flush()
        finally:
            self.f.close()

    # ---- bitmaps ----
    @staticmethod
    def _bit(bm, i):
        return (bm[i >> 3] >> (i & 7)) & 1

    @staticmethod
    def _set(bm, i):
        bm[i >> 3] |= 1 << (i & 7)

    @staticmethod
    def _clr(bm, i):
        bm[i >> 3] &= ~(1 << (i & 7)) & 0xFF

    def _find_free(self, bm, total, hint):
        if hint >= total:
            hint = 0
        for lo, hi in ((hint, total), (0, hint)):
            pos = lo >> 3
            end = (hi + 7) >> 3
            while pos < end:
                m = NOT_FF.search(bm, pos, end)
                if not m:
                    break
                i = m.start()
                byte = bm[i]
                for b in range(8):
                    idx = i * 8 + b
                    if idx < lo:
                        continue
                    if idx >= hi:
                        break
                    if not (byte >> b) & 1:
                        return idx
                pos = i + 1
        return None

    def alloc_block(self):
        sb = self.sb
        if sb["free_blocks"] == 0:
            raise YcfsError("filesystem full (no free blocks)")
        idx = self._find_free(self.bbm, sb["total_blocks"], sb["block_hint"])
        if idx is None:
            raise YcfsError("filesystem full (no free blocks)")
        self._set(self.bbm, idx)
        sb["free_blocks"] -= 1
        sb["block_hint"] = idx + 1
        self.bbm_dirty = self.sb_dirty = True
        return idx

    def free_block(self, b):
        sb = self.sb
        if b < sb["data_start_block"] or b >= sb["total_blocks"]:
            return
        if not self._bit(self.bbm, b):
            return
        self._clr(self.bbm, b)
        sb["free_blocks"] += 1
        if b < sb["block_hint"]:
            sb["block_hint"] = b
        self.bbm_dirty = self.sb_dirty = True

    def alloc_inode(self):
        sb = self.sb
        if sb["free_inodes"] == 0:
            raise YcfsError("filesystem full (no free inodes)")
        idx = self._find_free(self.ibm, sb["total_inodes"], sb["inode_hint"])
        if idx is None:
            raise YcfsError("filesystem full (no free inodes)")
        self._set(self.ibm, idx)
        sb["free_inodes"] -= 1
        sb["inode_hint"] = idx + 1
        self.ibm_dirty = self.sb_dirty = True
        return idx

    def free_inode(self, n):
        sb = self.sb
        if n == 0 or n >= sb["total_inodes"] or not self._bit(self.ibm, n):
            return
        self._clr(self.ibm, n)
        sb["free_inodes"] += 1
        if n < sb["inode_hint"]:
            sb["inode_hint"] = n
        self.ibm_dirty = self.sb_dirty = True

    # ---- inodes ----
    def _inode_pos(self, n):
        if n <= 0 or n >= self.sb["total_inodes"]:
            raise YcfsError("inode number %d out of range" % n)
        blk = self.sb["inode_table_block"] + n // INODES_PER_BLOCK
        return START + blk * BLOCK + (n % INODES_PER_BLOCK) * INODE_SIZE

    def read_inode(self, n):
        self.f.seek(self._inode_pos(n))
        raw = self.f.read(INODE_SIZE)
        raw += b"\x00" * (INODE_SIZE - len(raw))
        return Inode.unpack(raw)

    def write_inode(self, n, ino):
        self.f.seek(self._inode_pos(n))
        self.f.write(ino.pack())

    # ---- block mapping (mirrors the kernel's bmap) ----
    def _read_ptrs(self, blk):
        return list(struct.unpack("<%dI" % PPB, self.read_blk(blk)))

    def _write_ptrs(self, blk, ptrs):
        self.write_blk(blk, struct.pack("<%dI" % PPB, *ptrs))

    def _new_ptr_block(self, ino):
        b = self.alloc_block()
        self.write_blk(b, b"\x00" * BLOCK)
        ino.blocks_used += 1
        return b

    def bmap(self, ino, idx, alloc):
        """Physical block for logical block idx (0 = hole), allocating if asked."""
        if idx < DIRECT:
            if ino.direct[idx] == 0 and alloc:
                ino.direct[idx] = self.alloc_block()
                ino.blocks_used += 1
            return ino.direct[idx]
        idx -= DIRECT
        if idx < PPB:
            if ino.indirect == 0:
                if not alloc:
                    return 0
                ino.indirect = self._new_ptr_block(ino)
            ptrs = self._read_ptrs(ino.indirect)
            if ptrs[idx] == 0 and alloc:
                ptrs[idx] = self.alloc_block()
                ino.blocks_used += 1
                self._write_ptrs(ino.indirect, ptrs)
            return ptrs[idx]
        idx -= PPB
        if idx >= PPB * PPB:
            raise YcfsError("file too large (max ~4GB)")
        l1, l2 = divmod(idx, PPB)
        if ino.dindirect == 0:
            if not alloc:
                return 0
            ino.dindirect = self._new_ptr_block(ino)
        root = self._read_ptrs(ino.dindirect)
        if root[l1] == 0:
            if not alloc:
                return 0
            root[l1] = self._new_ptr_block(ino)
            self._write_ptrs(ino.dindirect, root)
        leaf = self._read_ptrs(root[l1])
        if leaf[l2] == 0 and alloc:
            leaf[l2] = self.alloc_block()
            ino.blocks_used += 1
            self._write_ptrs(root[l1], leaf)
        return leaf[l2]

    def block_map(self, ino):
        """(data_blocks, pointer_blocks): data_blocks[i] is logical block i's
        physical block (0 for holes)."""
        n = (ino.size + BLOCK - 1) // BLOCK
        data = [0] * n
        ptrs = []
        for i in range(min(n, DIRECT)):
            data[i] = ino.direct[i]
        if ino.indirect:
            ptrs.append(ino.indirect)
            p = self._read_ptrs(ino.indirect)
            for i in range(PPB):
                if DIRECT + i < n:
                    data[DIRECT + i] = p[i]
        if ino.dindirect:
            ptrs.append(ino.dindirect)
            root = self._read_ptrs(ino.dindirect)
            for a in range(PPB):
                if not root[a]:
                    continue
                ptrs.append(root[a])
                leaf = self._read_ptrs(root[a])
                for b in range(PPB):
                    li = DIRECT + PPB + a * PPB + b
                    if li < n:
                        data[li] = leaf[b]
        return data, ptrs

    def all_blocks(self, ino):
        """Every block an inode owns, data + pointer, including blocks
        beyond `size` (for freeing)."""
        blocks = [b for b in ino.direct if b]
        ptrs = []
        if ino.indirect:
            ptrs.append(ino.indirect)
            blocks += [b for b in self._read_ptrs(ino.indirect) if b]
        if ino.dindirect:
            ptrs.append(ino.dindirect)
            for r in self._read_ptrs(ino.dindirect):
                if r:
                    ptrs.append(r)
                    blocks += [b for b in self._read_ptrs(r) if b]
        return blocks, ptrs

    # ---- file data ----
    def read_data(self, ino, offset=0, length=None):
        """Yield bytes of an inode's data, coalescing contiguous blocks."""
        if length is None or offset + length > ino.size:
            length = max(0, ino.size - offset)
        data, _ = self.block_map(ino)
        end = offset + length
        pos = offset
        while pos < end:
            bi = pos // BLOCK
            run = 1
            if data[bi]:
                while (pos // BLOCK + run < len(data) and run < 4096 and
                       data[bi + run] == data[bi] + run and
                       (bi + run) * BLOCK < end):
                    run += 1
                self.f.seek(START + data[bi] * BLOCK)
                chunk = self.f.read(run * BLOCK)
                chunk += b"\x00" * (run * BLOCK - len(chunk))
            else:
                chunk = b"\x00" * BLOCK       # hole
            lo = pos - bi * BLOCK
            hi = min(len(chunk), end - bi * BLOCK)
            yield chunk[lo:hi]
            pos = bi * BLOCK + run * BLOCK

    def read_all(self, ino):
        return b"".join(self.read_data(ino))

    def free_inode_data(self, ino):
        blocks, ptrs = self.all_blocks(ino)
        for b in blocks + ptrs:
            self.free_block(b)
        ino.direct = [0] * DIRECT
        ino.indirect = ino.dindirect = 0
        ino.size = 0
        ino.blocks_used = 0

    def write_stream(self, ino, src, size):
        """Store `size` bytes read from file object `src` into inode `ino`
        (which must hold no data yet).  Allocates everything up front so a
        full disk fails before anything is written."""
        n = (size + BLOCK - 1) // BLOCK
        if n > MAX_FILE_BLOCKS:
            raise YcfsError("file too large: %s (max ~4GB)" % human(size))
        nptr = 0
        if n > DIRECT:
            nptr += 1
        if n > DIRECT + PPB:
            nptr += 1 + (n - DIRECT - PPB + PPB - 1) // PPB
        if n + nptr > self.sb["free_blocks"]:
            raise YcfsError("not enough free space: need %s, have %s" %
                            (human((n + nptr) * BLOCK), human(self.sb["free_blocks"] * BLOCK)))
        blocks = [self.alloc_block() for _ in range(n)]
        # data, one contiguous run at a time
        i = 0
        remaining = size
        while i < n:
            j = i + 1
            while j < n and blocks[j] == blocks[j - 1] + 1 and j - i < 4096:
                j += 1
            want = min(remaining, (j - i) * BLOCK)
            buf = src.read(want)
            if len(buf) != want:
                raise YcfsError("source file shrank while copying")
            buf += b"\x00" * ((j - i) * BLOCK - len(buf))
            self.f.seek(START + blocks[i] * BLOCK)
            self.f.write(buf)
            remaining -= want
            i = j
        # pointers
        for k in range(min(n, DIRECT)):
            ino.direct[k] = blocks[k]
        rest = blocks[DIRECT:]
        if rest:
            ino.indirect = self._new_ptr_block(ino)
            self._write_ptrs(ino.indirect, (rest[:PPB] + [0] * PPB)[:PPB])
            rest = rest[PPB:]
        if rest:
            ino.dindirect = self._new_ptr_block(ino)
            root = [0] * PPB
            for a in range(0, len(rest), PPB):
                leaf_blk = self._new_ptr_block(ino)
                chunk = rest[a:a + PPB]
                self._write_ptrs(leaf_blk, (chunk + [0] * PPB)[:PPB])
                root[a // PPB] = leaf_blk
            self._write_ptrs(ino.dindirect, root)
        ino.size = size
        ino.blocks_used += n

    # ---- directories ----
    def dir_entries(self, dir_ino):
        """[(slot_offset, name, inode_number, type)] for live entries."""
        raw = self.read_all(dir_ino)
        out = []
        for off in range(0, len(raw) - DIRENT_SIZE + 1, DIRENT_SIZE):
            inum, _rl, nlen, ftype, name = struct.unpack_from(DIRENT_FMT, raw, off)
            if inum == 0:
                continue
            out.append((off, name[:nlen].decode("utf-8", "replace"), inum, ftype))
        return out

    def lookup(self, dir_num, name):
        d = self.read_inode(dir_num)
        if d.mode != TYPE_DIR:
            return None
        for _off, n, inum, _t in self.dir_entries(d):
            if n == name:
                return inum
        return None

    def _write_dir_bytes(self, dir_num, dino, offset, data):
        """Small write into a directory's data (allocates as needed)."""
        pos = 0
        while pos < len(data):
            bi = (offset + pos) // BLOCK
            bo = (offset + pos) % BLOCK
            phys = self.bmap(dino, bi, True)
            chunk = min(BLOCK - bo, len(data) - pos)
            blk = bytearray(self.read_blk(phys))
            blk[bo:bo + chunk] = data[pos:pos + chunk]
            self.write_blk(phys, bytes(blk))
            pos += chunk
        if offset + len(data) > dino.size:
            dino.size = offset + len(data)
        self.write_inode(dir_num, dino)

    def add_dirent(self, dir_num, name, inum, ftype):
        nb = name.encode("utf-8")
        if not nb or len(nb) > 55 or b"/" in nb:
            raise YcfsError("bad file name %r (1-55 bytes, no '/')" % name)
        dino = self.read_inode(dir_num)
        rec = struct.pack(DIRENT_FMT, inum, DIRENT_SIZE, len(nb), ftype, nb.ljust(56, b"\x00"))
        raw = self.read_all(dino)
        slot = None
        for off in range(0, len(raw) - DIRENT_SIZE + 1, DIRENT_SIZE):
            if struct.unpack_from("<I", raw, off)[0] == 0:
                slot = off
                break
        self._write_dir_bytes(dir_num, dino, dino.size if slot is None else slot, rec)

    def remove_dirent(self, dir_num, name):
        dino = self.read_inode(dir_num)
        for off, n, _inum, _t in self.dir_entries(dino):
            if n == name:
                self._write_dir_bytes(dir_num, dino, off, struct.pack("<I", 0))
                return
        raise YcfsError("no such entry %r" % name)

    # ---- paths ----
    @staticmethod
    def split(path):
        p = path.replace("\\", "/")
        parts = [x for x in p.split("/") if x not in ("", ".")]
        if parts and parts[0] == "ycfs":
            parts = parts[1:]
        out = []
        for x in parts:
            if x == "..":
                if out:
                    out.pop()
            else:
                out.append(x)
        return out

    def resolve(self, path):
        cur = ROOT_INODE
        for part in self.split(path):
            nxt = self.lookup(cur, part)
            if nxt is None:
                return None
            cur = nxt
        return cur

    def resolve_parent(self, path):
        parts = self.split(path)
        if not parts:
            raise YcfsError("path %r has no file name" % path)
        cur = ROOT_INODE
        for part in parts[:-1]:
            nxt = self.lookup(cur, part)
            if nxt is None:
                raise YcfsError("no such directory: /%s" % "/".join(parts[:parts.index(part) + 1]))
            if self.read_inode(nxt).mode != TYPE_DIR:
                raise YcfsError("not a directory: %s" % part)
            cur = nxt
        return cur, parts[-1]

    # ---- high-level operations ----
    def mkdir(self, path, parents=False, uid=0, gid=0, perm=0o755):
        parts = self.split(path)
        cur = ROOT_INODE
        for i, part in enumerate(parts):
            nxt = self.lookup(cur, part)
            if nxt is not None:
                if self.read_inode(nxt).mode != TYPE_DIR:
                    raise YcfsError("%s exists and is not a directory" % part)
                cur = nxt
                continue
            if i < len(parts) - 1 and not parents:
                raise YcfsError("no such directory: /%s (use -p)" % "/".join(parts[:i + 1]))
            n = self.alloc_inode()
            ino = Inode(TYPE_DIR)
            ino.uid, ino.gid, ino.perm = uid, gid, perm
            ino.mtime = int(time.time())
            self.write_inode(n, ino)
            self.add_dirent(cur, part, n, TYPE_DIR)
            cur = n
        return cur

    def put_stream(self, path, src, size, uid=0, gid=0, perm=0o644, mtime=None, overwrite=True):
        parent, name = self.resolve_parent(path)
        existing = self.lookup(parent, name)
        if existing is not None:
            if not overwrite:
                raise YcfsError("%s already exists" % path)
            old = self.read_inode(existing)
            if old.mode != TYPE_FILE:
                raise YcfsError("%s exists and is not a regular file" % path)
            self.free_inode_data(old)
            ino, num = old, existing
            ino.uid, ino.gid, ino.perm = uid, gid, perm
        else:
            num = self.alloc_inode()
            ino = Inode(TYPE_FILE)
            ino.uid, ino.gid, ino.perm = uid, gid, perm
        ino.mtime = int(mtime if mtime is not None else time.time())
        self.write_stream(ino, src, size)
        self.write_inode(num, ino)
        if existing is None:
            self.add_dirent(parent, name, num, TYPE_FILE)
        return num

    def put_bytes(self, path, data, **kw):
        import io
        return self.put_stream(path, io.BytesIO(data), len(data), **kw)

    def remove(self, path, recursive=False):
        parent, name = self.resolve_parent(path)
        num = self.lookup(parent, name)
        if num is None:
            raise YcfsError("no such file or directory: %s" % path)
        self._remove_inode(parent, name, num, recursive)

    def _remove_inode(self, parent, name, num, recursive):
        ino = self.read_inode(num)
        if ino.mode == TYPE_DIR:
            kids = self.dir_entries(ino)
            if kids and not recursive:
                raise YcfsError("%s is a non-empty directory (use -r)" % name)
            for _off, kname, knum, _t in kids:
                self._remove_inode(num, kname, knum, True)
        self.remove_dirent(parent, name)
        self.free_inode_data(ino)
        self.write_inode(num, Inode(0))
        self.free_inode(num)

    def walk(self, num=ROOT_INODE, prefix=""):
        """Yield (path, inum, Inode) for everything under `num` (depth-first, sorted)."""
        d = self.read_inode(num)
        for _off, name, inum, _t in sorted(self.dir_entries(d), key=lambda e: e[1]):
            ino = self.read_inode(inum)
            p = prefix + "/" + name
            yield p, inum, ino
            if ino.mode == TYPE_DIR:
                yield from self.walk(inum, p)


# --------------------------------------------------------------------------
def mkfs(path, size=None, bytes_per_inode=32768, alloc_start_gb=0):
    """Create a fresh YCFS in `path`, leaving the first 64MB (FAT16) alone."""
    if not os.path.exists(path):
        raise YcfsError("%s does not exist (expected the existing disk image with the FAT16 area)" % path)
    cur = os.path.getsize(path)
    if size is not None:
        if size < START + 16 * 1024 * 1024:
            raise YcfsError("size must be at least %s" % human(START + 16 * 1024 * 1024))
        if size < cur:
            raise YcfsError("%s is already %s; refusing to shrink it" % (path, human(cur)))
        if size > cur:
            with open(path, "r+b") as f:
                f.truncate(size)
            print("grew %s from %s to %s (sparse)" % (path, human(cur), human(size)))
        cur = size
    if cur < START + 16 * 1024 * 1024:
        raise YcfsError("%s is only %s; a YCFS needs at least %s (pass --size)" %
                        (path, human(cur), human(START + 16 * 1024 * 1024)))
    region = cur - START
    g = geometry(region, bytes_per_inode)
    hint = g["data_start_block"]
    if alloc_start_gb:
        hint = max(hint, int(alloc_start_gb * (1 << 30)) // BLOCK)
        if hint >= g["total_blocks"] - 1024:
            raise YcfsError("--alloc-start-gb %s is past the end of the %s filesystem" %
                            (alloc_start_gb, human(g["total_blocks"] * BLOCK)))
    with open(path, "r+b") as f:
        # metadata areas: write zeros only where they must be non-zero
        total_blocks, total_inodes = g["total_blocks"], g["total_inodes"]
        sb = dict(magic=MAGIC, version=2, block_size=BLOCK,
                  total_blocks=total_blocks, total_inodes=total_inodes,
                  free_blocks=total_blocks - g["data_start_block"],
                  free_inodes=total_inodes - 2,           # inode 0 reserved + root
                  inode_bitmap_block=g["inode_bitmap_block"],
                  block_bitmap_block=g["block_bitmap_block"],
                  inode_table_block=g["inode_table_block"],
                  inode_table_blocks=g["inode_table_blocks"],
                  data_start_block=g["data_start_block"], root_inode=ROOT_INODE,
                  journal_start_block=g["journal_start_block"],
                  journal_num_blocks=JOURNAL_BLOCKS,
                  block_bitmap_blocks=g["block_bitmap_blocks"],
                  inode_bitmap_blocks=g["inode_bitmap_blocks"],
                  block_hint=hint, inode_hint=2)
        raw = struct.pack(SB_FMT, *[sb[k] for k in SB_FIELDS])
        f.seek(START)
        f.write(raw + b"\x00" * (BLOCK - len(raw)))

        # block bitmap: metadata blocks used; bits past the end marked used too
        bbm = bytearray(g["block_bitmap_blocks"] * BLOCK)
        used = g["data_start_block"]
        full = used // 8
        bbm[:full] = b"\xff" * full
        for i in range(full * 8, used):
            bbm[i >> 3] |= 1 << (i & 7)
        for i in range(total_blocks, g["block_bitmap_blocks"] * BITS):
            bbm[i >> 3] |= 1 << (i & 7)
        f.seek(START + g["block_bitmap_block"] * BLOCK)
        f.write(bytes(bbm))

        ibm = bytearray(g["inode_bitmap_blocks"] * BLOCK)
        ibm[0] = 0b11                                   # inode 0 (reserved) + root
        for i in range(total_inodes, g["inode_bitmap_blocks"] * BITS):
            ibm[i >> 3] |= 1 << (i & 7)
        f.seek(START + g["inode_bitmap_block"] * BLOCK)
        f.write(bytes(ibm))

        # journal: explicit zeros (a stale old journal must not be replayed)
        f.seek(START + g["journal_start_block"] * BLOCK)
        f.write(b"\x00" * (JOURNAL_BLOCKS * BLOCK))

        # inode table: only the block holding inodes 0/1 needs content; the
        # rest is zeros, which we write explicitly for the first block and
        # leave sparse otherwise -- BUT an old image may hold stale inodes
        # there, so zero it for real when the region already has data.
        itb_bytes = g["inode_table_blocks"] * BLOCK
        f.seek(START + g["inode_table_block"] * BLOCK)
        root = Inode(TYPE_DIR)
        root.mtime = int(time.time())
        first = bytearray(BLOCK)
        first[INODE_SIZE:2 * INODE_SIZE] = root.pack()
        f.write(bytes(first))
        _zero_range(f, START + g["inode_table_block"] * BLOCK + BLOCK, itb_bytes - BLOCK)
    return g


def _zero_range(f, offset, length):
    """Zero [offset, offset+length) of an open file.  Over a sparse hole
    nothing is written (stale data can only exist where the file was
    actually allocated), so a fresh multi-TB image stays tiny."""
    f.flush()        # we use raw os.lseek below; pending buffered writes
                     # must hit the file first or they land at the wrong offset
    end = os.fstat(f.fileno()).st_size
    stop = min(offset + length, end)
    pos = offset
    chunk = 8 * 1024 * 1024
    zero = b"\x00" * chunk
    if hasattr(os, "SEEK_DATA"):
        try:
            while pos < stop:
                try:
                    d = os.lseek(f.fileno(), pos, os.SEEK_DATA)
                except OSError:
                    break                      # no more data after pos
                if d >= stop:
                    break
                h = os.lseek(f.fileno(), d, os.SEEK_HOLE)
                h = min(h, stop)
                p = d
                while p < h:
                    n = min(chunk, h - p)
                    f.seek(p)
                    f.write(zero[:n])
                    p += n
                pos = h
            return
        except OSError:
            pass
    pos = offset
    while pos < stop:                           # filesystem without hole info
        n = min(chunk, stop - pos)
        f.seek(pos)
        f.write(zero[:n])
        pos += n


def export_tar(img, out_path):
    count = 0
    with tarfile.open(out_path, "w") as tf:
        for path, _num, ino in img.walk():
            ti = tarfile.TarInfo(path.lstrip("/"))
            ti.uid, ti.gid = ino.uid, ino.gid
            ti.mode = ino.perm & 0o777
            ti.mtime = ino.mtime
            if ino.mode == TYPE_DIR:
                ti.type = tarfile.DIRTYPE
                tf.addfile(ti)
            else:
                import io
                data = img.read_all(ino)
                ti.size = len(data)
                tf.addfile(ti, io.BytesIO(data))
            count += 1
    return count


def import_tar(img, in_path):
    count = 0
    with tarfile.open(in_path, "r") as tf:
        members = sorted(tf.getmembers(), key=lambda m: (m.name.count("/"), m.name))
        for m in members:
            path = "/" + m.name
            if m.isdir():
                img.mkdir(path, parents=True, uid=m.uid, gid=m.gid, perm=m.mode & 0o777)
            elif m.isfile():
                parts = img.split(path)
                if len(parts) > 1:
                    img.mkdir("/" + "/".join(parts[:-1]), parents=True)
                img.put_stream(path, tf.extractfile(m), m.size, uid=m.uid, gid=m.gid,
                               perm=m.mode & 0o777, mtime=m.mtime)
            else:
                continue
            count += 1
    return count


def fsck(img, verbose=True):
    """Walk the tree and cross-check bitmaps and counters.  Returns the
    number of problems found."""
    sb = img.sb
    problems = []

    def bad(msg):
        problems.append(msg)
        if len(problems) <= 40:
            print("  PROBLEM:", msg)

    total_blocks, total_inodes = sb["total_blocks"], sb["total_inodes"]
    expect_b = bytearray(len(img.bbm))
    for i in range(sb["data_start_block"]):                  # metadata
        expect_b[i >> 3] |= 1 << (i & 7)
    expect_i = bytearray(len(img.ibm))
    expect_i[0] |= 1
    owner = {}
    used_blocks = sb["data_start_block"]
    seen_inodes = set()

    def claim(blk, who):
        nonlocal used_blocks
        if blk < sb["data_start_block"] or blk >= total_blocks:
            bad("%s uses out-of-range block %d" % (who, blk))
            return
        if (expect_b[blk >> 3] >> (blk & 7)) & 1:
            bad("block %d is used twice (%s and %s)" % (blk, owner.get(blk, "?"), who))
            return
        expect_b[blk >> 3] |= 1 << (blk & 7)
        owner[blk] = who
        used_blocks += 1

    def visit(num, path):
        if num in seen_inodes:
            bad("inode %d reachable twice (%s)" % (num, path))
            return
        seen_inodes.add(num)
        if num <= 0 or num >= total_inodes:
            bad("%s: inode %d out of range" % (path, num))
            return
        expect_i[num >> 3] |= 1 << (num & 7)
        ino = img.read_inode(num)
        if ino.mode not in (TYPE_FILE, TYPE_DIR):
            bad("%s: inode %d has bad mode %d" % (path, num, ino.mode))
            return
        data, ptrs = img.block_map(ino)
        allb, allp = img.all_blocks(ino)
        for b in allb + allp:
            claim(b, "%s (inode %d)" % (path, num))
        if ino.blocks_used != len(allb) + len(allp):
            bad("%s: blocks_used=%d but owns %d blocks" % (path, ino.blocks_used, len(allb) + len(allp)))
        need = (ino.size + BLOCK - 1) // BLOCK
        if need > MAX_FILE_BLOCKS:
            bad("%s: size %d exceeds the maximum" % (path, ino.size))
        if ino.mode == TYPE_DIR:
            if ino.size % DIRENT_SIZE:
                bad("%s: directory size %d not a multiple of %d" % (path, ino.size, DIRENT_SIZE))
            names = set()
            for _off, name, inum, ftype in img.dir_entries(ino):
                if name in names:
                    bad("%s: duplicate name %r" % (path, name))
                names.add(name)
                if 0 < inum < total_inodes:
                    child = img.read_inode(inum)
                    if child.mode != ftype:
                        bad("%s/%s: dirent says type %d, inode says %d" % (path, name, ftype, child.mode))
                visit(inum, path + "/" + name)

    visit(sb["root_inode"], "")
    # compare bitmaps
    limit_b = total_blocks
    for i in range(0, len(img.bbm)):
        a, e = img.bbm[i], expect_b[i]
        if a == e:
            continue
        for bit in range(8):
            idx = i * 8 + bit
            if idx >= limit_b:
                break
            av, ev = (a >> bit) & 1, (e >> bit) & 1
            if av and not ev:
                bad("block %d marked used but nothing references it (leak)" % idx)
            elif ev and not av:
                bad("block %d is in use but marked free (DANGEROUS)" % idx)
    for i in range(0, len(img.ibm)):
        a, e = img.ibm[i], expect_i[i]
        if a == e:
            continue
        for bit in range(8):
            idx = i * 8 + bit
            if idx >= total_inodes:
                break
            av, ev = (a >> bit) & 1, (e >> bit) & 1
            if av and not ev:
                bad("inode %d marked used but unreachable (orphan)" % idx)
            elif ev and not av:
                bad("inode %d is in use but marked free (DANGEROUS)" % idx)
    exp_free_b = total_blocks - used_blocks
    exp_free_i = total_inodes - len(seen_inodes) - 1
    if sb["free_blocks"] != exp_free_b:
        bad("superblock free_blocks=%d, actual %d" % (sb["free_blocks"], exp_free_b))
    if sb["free_inodes"] != exp_free_i:
        bad("superblock free_inodes=%d, actual %d" % (sb["free_inodes"], exp_free_i))
    if verbose:
        print("fsck: %d inodes, %d blocks in use -> %s" %
              (len(seen_inodes), used_blocks - sb["data_start_block"],
               "OK" if not problems else "%d PROBLEM(S)" % len(problems)))
    return len(problems)


def _set_bits(bm):
    """Yield the index of every 1 bit (fast over mostly-empty bitmaps)."""
    for m in re.finditer(rb"[^\x00]", bytes(bm)):
        i = m.start()
        byte = bm[i]
        for b in range(8):
            if (byte >> b) & 1:
                yield i * 8 + b


def repair(img):
    """Fix what fsck reports, without losing data:
      1. inodes that are in use but no folder lists any more (orphans -- e.g.
         left behind by the old bug where deleting a folder forgot its
         contents) are linked into /lost+found so their data is recoverable;
      2. both bitmaps are rebuilt from what is actually reachable, which
         frees leaked blocks/inodes and re-marks anything in use;
      3. the superblock counters and each inode's block count are recomputed.
    Blocks claimed by two files are only reported (they need a human)."""
    sb = img.sb
    total_blocks, total_inodes = sb["total_blocks"], sb["total_inodes"]
    report = {"orphans_recovered": 0, "orphans_discarded": 0, "leaked_blocks_freed": 0,
              "unmarked_blocks_fixed": 0, "inode_bits_fixed": 0, "blocks_used_fixed": 0,
              "double_claimed": []}

    def reachable():
        seen = {ROOT_INODE}
        stack = [ROOT_INODE]
        while stack:
            n = stack.pop()
            ino = img.read_inode(n)
            if ino.mode != TYPE_DIR:
                continue
            for _o, _name, c, _t in img.dir_entries(ino):
                if 0 < c < total_inodes and c not in seen:
                    seen.add(c)
                    stack.append(c)
        return seen

    reach = reachable()
    orphans = [n for n in _set_bits(img.ibm) if 1 <= n < total_inodes and n not in reach]
    candidates = {}
    for n in orphans:
        ino = img.read_inode(n)
        if ino.mode in (TYPE_FILE, TYPE_DIR):
            candidates[n] = ino
        else:
            report["orphans_discarded"] += 1          # an in-use bit with no real inode behind it
    children = set()
    for n, ino in candidates.items():
        if ino.mode == TYPE_DIR:
            for _o, _name, c, _t in img.dir_entries(ino):
                if c in candidates and c != n:
                    children.add(c)
    top = sorted(n for n in candidates if n not in children and n != ROOT_INODE)
    if top:
        lf = img.mkdir("/lost+found", parents=True)
        for n in top:
            name = "#%d" % n
            if img.lookup(lf, name) is None:
                img.add_dirent(lf, name, n, candidates[n].mode)
            report["orphans_recovered"] += 1

    reach = reachable()
    # expected bitmaps from reachability
    exp_b = bytearray(len(img.bbm))
    for i in range(sb["data_start_block"]):
        exp_b[i >> 3] |= 1 << (i & 7)
    exp_i = bytearray(len(img.ibm))
    exp_i[0] |= 1
    used_blocks = sb["data_start_block"]
    owner = {}
    for n in sorted(reach):
        exp_i[n >> 3] |= 1 << (n & 7)
        ino = img.read_inode(n)
        blocks, ptrs = img.all_blocks(ino)
        mine = 0
        for b in blocks + ptrs:
            if b < sb["data_start_block"] or b >= total_blocks:
                continue
            if (exp_b[b >> 3] >> (b & 7)) & 1:
                report["double_claimed"].append((b, owner.get(b), n))
                continue
            exp_b[b >> 3] |= 1 << (b & 7)
            owner[b] = n
            used_blocks += 1
            mine += 1
        if ino.blocks_used != len(blocks) + len(ptrs):
            ino.blocks_used = len(blocks) + len(ptrs)
            img.write_inode(n, ino)
            report["blocks_used_fixed"] += 1
    # bits past the end stay set, exactly as mkfs leaves them
    for i in range(total_blocks, len(exp_b) * 8):
        exp_b[i >> 3] |= 1 << (i & 7)
    for i in range(total_inodes, len(exp_i) * 8):
        exp_i[i >> 3] |= 1 << (i & 7)

    # count what changes, then apply
    for idx in range(len(img.bbm)):
        a, e = img.bbm[idx], exp_b[idx]
        if a == e:
            continue
        for bit in range(8):
            bi = idx * 8 + bit
            if bi >= total_blocks:
                break
            av, ev = (a >> bit) & 1, (e >> bit) & 1
            if av and not ev:
                report["leaked_blocks_freed"] += 1
            elif ev and not av:
                report["unmarked_blocks_fixed"] += 1
    for idx in range(len(img.ibm)):
        if img.ibm[idx] != exp_i[idx]:
            report["inode_bits_fixed"] += 1
    img.bbm[:] = exp_b
    img.ibm[:] = exp_i
    img.bbm_dirty = img.ibm_dirty = True
    sb["free_blocks"] = total_blocks - used_blocks
    sb["free_inodes"] = total_inodes - len(reach) - 1
    sb["block_hint"] = min(sb["block_hint"], max(sb["data_start_block"], sb["block_hint"]))
    img.sb_dirty = True
    return report


# --------------------------------------------------------------------------
def cmd_info(a):
    img = Image(a.img)
    sb = img.sb
    region = sb["total_blocks"] * BLOCK
    print("YCFS v%d at %s + 64MB" % (sb["version"], a.img))
    print("  size        %s (%d blocks of %d bytes)" % (human(region), sb["total_blocks"], BLOCK))
    print("  free        %s (%d blocks)" % (human(sb["free_blocks"] * BLOCK), sb["free_blocks"]))
    print("  inodes      %d total, %d free" % (sb["total_inodes"], sb["free_inodes"]))
    print("  bitmaps     %d block-bitmap blocks, %d inode-bitmap blocks" %
          (sb["block_bitmap_blocks"], sb["inode_bitmap_blocks"]))
    print("  data starts at block %d; journal %d blocks @ %d" %
          (sb["data_start_block"], sb["journal_num_blocks"], sb["journal_start_block"]))
    print("  journal     %s" % ("UNFINISHED transaction present" if img.journal_dirty() else "clean"))
    print("  max file    %s" % ("~4 GB" if sb["version"] == 2 else "~4 MB (v1; run migrate to lift)"))
    img.close()


def cmd_ls(a):
    img = Image(a.img)
    num = img.resolve(a.path)
    if num is None:
        raise YcfsError("no such file or directory: %s" % a.path)
    root = img.read_inode(num)

    def line(name, ino):
        t = "d" if ino.mode == TYPE_DIR else "-"
        perm = "".join(("r" if ino.perm & (4 << s) else "-") + ("w" if ino.perm & (2 << s) else "-") +
                       ("x" if ino.perm & (1 << s) else "-") for s in (6, 3, 0))
        print("%s%s %4d %4d %12d  %s" % (t, perm, ino.uid, ino.gid, ino.size, name))

    if root.mode != TYPE_DIR:
        line(a.path, root)
    elif a.recursive:
        base = "/" + "/".join(img.split(a.path))
        for p, _n, ino in img.walk(num, "" if base == "/" else base):
            line(p, ino)
    else:
        for _off, name, inum, _t in sorted(img.dir_entries(root), key=lambda e: e[1]):
            line(name, img.read_inode(inum))
    img.close()


def cmd_put(a):
    img = Image(a.img, write=True)
    try:
        img.require_clean_for_write(a.force)
        *srcs, dest = a.paths
        if not srcs:
            raise YcfsError("usage: put IMG SRC [SRC...] DEST")
        dnum = img.resolve(dest)
        dest_is_dir = dnum is not None and img.read_inode(dnum).mode == TYPE_DIR
        if len(srcs) > 1 and not dest_is_dir and not dest.endswith("/"):
            raise YcfsError("with several sources DEST must be a directory (end it with /)")
        for src in srcs:
            if os.path.isdir(src):
                for root, _dirs, files in os.walk(src):
                    rel = os.path.relpath(root, src)
                    base = dest.rstrip("/") + "/" + os.path.basename(src.rstrip("/"))
                    d = base if rel == "." else base + "/" + rel
                    img.mkdir(d, parents=True)
                    for fn in sorted(files):
                        _put_one(img, os.path.join(root, fn), d + "/" + fn, a)
                continue
            target = dest.rstrip("/") + "/" + os.path.basename(src) if (dest_is_dir or dest.endswith("/")) else dest
            if dest.endswith("/") and not dest_is_dir:
                img.mkdir(dest, parents=True)
            _put_one(img, src, target, a)
    finally:
        img.close()


def _put_one(img, src, target, a):
    st = os.stat(src)
    with open(src, "rb") as f:
        img.put_stream(target, f, st.st_size, uid=a.uid, gid=a.gid, perm=int(a.perm, 8),
                       mtime=st.st_mtime)
    print("  %s -> %s (%s)" % (src, target, human(st.st_size)))


def cmd_get(a):
    img = Image(a.img)
    num = img.resolve(a.path)
    if num is None:
        raise YcfsError("no such file: %s" % a.path)
    ino = img.read_inode(num)
    if ino.mode != TYPE_FILE:
        raise YcfsError("%s is a directory (use export for trees)" % a.path)
    dest = a.dest or os.path.basename(a.path)
    if os.path.isdir(dest):
        dest = os.path.join(dest, os.path.basename(a.path))
    with open(dest, "wb") as out:
        for chunk in img.read_data(ino):
            out.write(chunk)
    print("%s -> %s (%s)" % (a.path, dest, human(ino.size)))
    img.close()


def cmd_mkdir(a):
    img = Image(a.img, write=True)
    try:
        img.require_clean_for_write(a.force)
        img.mkdir(a.path, parents=a.parents)
    finally:
        img.close()


def cmd_rm(a):
    img = Image(a.img, write=True)
    try:
        img.require_clean_for_write(a.force)
        img.remove(a.path, recursive=a.recursive)
    finally:
        img.close()


def cmd_fsck(a):
    if a.repair:
        img = Image(a.img, write=True)
        try:
            img.require_clean_for_write(a.force)
            print("before repair:")
            before = fsck(img)
            if before == 0:
                print("nothing to repair")
                return 0
            r = repair(img)
            img.flush()
        finally:
            img.close()
        print("repair: %d orphan file(s)/folder(s) moved to /lost+found, %d discarded, "
              "%d leaked block(s) freed, %d block(s) re-marked in use, %d inode bit(s) fixed, "
              "%d inode block-count(s) fixed" %
              (r["orphans_recovered"], r["orphans_discarded"], r["leaked_blocks_freed"],
               r["unmarked_blocks_fixed"], r["inode_bits_fixed"], r["blocks_used_fixed"]))
        for b, o1, o2 in r["double_claimed"][:10]:
            print("  WARNING: block %d claimed by inodes %s and %s -- needs a human" % (b, o1, o2))
        img = Image(a.img)
        print("after repair:")
        n = fsck(img)
        img.close()
        if r["orphans_recovered"]:
            print("recovered items are in /lost+found (named #<inode>); look through them, "
                  "keep what you need and delete the rest.")
        return 1 if (n or r["double_claimed"]) else 0
    img = Image(a.img)
    n = fsck(img)
    img.close()
    return 1 if n else 0


def cmd_mkfs(a):
    if not a.yes:
        try:
            Image(a.img).close()
            raise YcfsError("%s already contains a YCFS; mkfs would ERASE every file in it "
                            "(accounts, settings, wallpaper...). Re-run with --yes to confirm, "
                            "or use `migrate` to keep your files." % a.img)
        except YcfsError as e:
            if "ERASE" in str(e):
                raise
    g = mkfs(a.img, parse_size(a.size) if a.size else None, a.bytes_per_inode, a.alloc_start_gb)
    print("YCFS formatted: %s, %d inodes" % (human(g["total_blocks"] * BLOCK), g["total_inodes"]))


def cmd_export(a):
    img = Image(a.img)
    n = export_tar(img, a.out)
    img.close()
    print("exported %d entries to %s" % (n, a.out))


def cmd_import(a):
    img = Image(a.img, write=True)
    try:
        img.require_clean_for_write(a.force)
        n = import_tar(img, a.tar)
    finally:
        img.close()
    print("imported %d entries" % n)


def cmd_migrate(a):
    size = parse_size(a.size) if a.size else None
    img = Image(a.img)
    if img.journal_dirty() and not a.force:
        img.close()
        raise YcfsError("journal has an unfinished transaction: boot the OS once and shut down "
                        "cleanly first (or --force).")
    old_ver = img.sb["version"]
    backup = "%s.ycfs-backup-%s.tar" % (a.img, time.strftime("%Y%m%d-%H%M%S"))
    n = export_tar(img, backup)
    img.close()
    print("saved %d entries from the old YCFS (v%d) to %s" % (n, old_ver, backup))
    g = mkfs(a.img, size, a.bytes_per_inode)
    img = Image(a.img, write=True)
    try:
        m = import_tar(img, backup)
    finally:
        img.close()
    img = Image(a.img)
    problems = fsck(img)
    img.close()
    print("migrated: %d entries restored into a fresh v2 YCFS of %s (%d inodes)" %
          (m, human(g["total_blocks"] * BLOCK), g["total_inodes"]))
    print("keep %s until you have booted and checked everything." % backup)
    return 1 if problems else 0


def main(argv=None):
    p = argparse.ArgumentParser(description="YCFS image tool", formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog="Shut QEMU down before modifying an image.")
    sub = p.add_subparsers(dest="cmd", required=True)

    def add(name, fn, help_):
        sp = sub.add_parser(name, help=help_)
        sp.add_argument("img")
        sp.set_defaults(fn=fn)
        return sp

    add("info", cmd_info, "show geometry and usage")
    sp = add("ls", cmd_ls, "list a directory")
    sp.add_argument("path", nargs="?", default="/")
    sp.add_argument("-R", "--recursive", action="store_true")
    sp = add("put", cmd_put, "copy host files/directories in")
    sp.add_argument("paths", nargs="+", metavar="SRC... DEST")
    sp.add_argument("--uid", type=int, default=0)
    sp.add_argument("--gid", type=int, default=0)
    sp.add_argument("--perm", default="644", help="octal permission bits (default 644)")
    sp.add_argument("--force", action="store_true")
    sp = add("get", cmd_get, "copy a file out")
    sp.add_argument("path")
    sp.add_argument("dest", nargs="?")
    sp = add("mkdir", cmd_mkdir, "make a directory")
    sp.add_argument("path")
    sp.add_argument("-p", "--parents", action="store_true")
    sp.add_argument("--force", action="store_true")
    sp = add("rm", cmd_rm, "remove a file or directory")
    sp.add_argument("path")
    sp.add_argument("-r", "--recursive", action="store_true")
    sp.add_argument("--force", action="store_true")
    sp = add("fsck", cmd_fsck, "check consistency (--repair to fix)")
    sp.add_argument("--repair", action="store_true",
                    help="recover orphaned files into /lost+found and rebuild bitmaps/counters")
    sp.add_argument("--force", action="store_true")
    sp = add("mkfs", cmd_mkfs, "format (ERASES /ycfs)")
    sp.add_argument("--size", help="total disk image size, e.g. 4G (grows the file, sparse)")
    sp.add_argument("--bytes-per-inode", type=int, default=32768)
    sp.add_argument("--yes", action="store_true", help="confirm erasing an existing YCFS")
    sp.add_argument("--alloc-start-gb", type=float, default=0,
                    help="TESTING: start allocating files this many GB into the disk, so a "
                         "test image exercises sectors beyond 128GB (the LBA48 range)")
    sp = add("export", cmd_export, "dump everything to a tar archive")
    sp.add_argument("out")
    sp = add("import", cmd_import, "add the contents of a tar archive")
    sp.add_argument("tar")
    sp.add_argument("--force", action="store_true")
    sp = add("migrate", cmd_migrate, "reformat to the current layout/size keeping all files")
    sp.add_argument("--size", help="new total image size, e.g. 4G")
    sp.add_argument("--bytes-per-inode", type=int, default=32768)
    sp.add_argument("--force", action="store_true")

    a = p.parse_args(argv)
    try:
        return a.fn(a) or 0
    except YcfsError as e:
        print("ycfs: error:", e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
