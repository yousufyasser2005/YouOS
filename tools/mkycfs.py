#!/usr/bin/env python3
"""
mkycfs.py — Format the YCFS region inside disk.img.

Usage: python3 tools/mkycfs.py disk.img

Writes a fresh YCFS superblock/bitmaps/inode-table/seed-data starting at
byte offset 64MB into disk.img, growing the file if needed to fit the
full 16MB YCFS region. Never touches bytes before that offset (FAT16's
own area is left completely alone).

Seeds a small tree to verify real nested directories work:
  /ycfs/hello.txt
  /ycfs/docs/notes.txt

Also seeds two permission-test files for exercising Phase 3.5 item #4
(stage 3 permission enforcement):
  /ycfs/roottest.txt  — uid 0 (root), perm 0600 (owner rw only)
  /ycfs/usertest.txt  — uid 1 (first non-root user created via
                         auth_create_user, e.g. via Settings > Add
                         Account), perm 0600 (owner rw only)

Also seeds a test WAV file for the media-support effort's audio driver
work, read from an external path on the host (not part of the repo):
  /ycfs/ding.wav
"""
import struct, sys, os

BLOCK_SIZE = 4096
YCFS_START_LBA = 131072
YCFS_START = YCFS_START_LBA * 512          # 64MB
REGION_SIZE = 16 * 1024 * 1024             # 16MB
TOTAL_BLOCKS = REGION_SIZE // BLOCK_SIZE   # 4096
TOTAL_INODES = 1024

MAGIC = 0x59434653
YCFS_TYPE_FILE = 1
YCFS_TYPE_DIR  = 2

INODE_BITMAP_BLOCK = 1
BLOCK_BITMAP_BLOCK = 2
INODE_TABLE_BLOCK  = 3
INODE_SIZE = 128
INODE_TABLE_BLOCKS = (TOTAL_INODES * INODE_SIZE + BLOCK_SIZE - 1) // BLOCK_SIZE
JOURNAL_START_BLOCK = INODE_TABLE_BLOCK + INODE_TABLE_BLOCKS
JOURNAL_BLOCKS = 128
DATA_START_BLOCK = JOURNAL_START_BLOCK + JOURNAL_BLOCKS
ROOT_INODE = 1

# External asset — not part of the repo, read from the host filesystem
# at format time. If missing, ding.wav is simply skipped (mkycfs.py
# still works fine without it, same as wall.bmp's optional handling).
DING_WAV_PATH = "/home/yousuf/codes/os/assets/sounds/ding.wav"

# Same pattern: external asset, not part of the repo, read at format
# time. If missing, test.png is simply skipped.
TEST_PNG_PATH = "/home/yousuf/Pictures/Screenshots/Screenshot from 2026-01-13 10-28-37.png"


def pack_superblock(free_blocks, free_inodes):
    fields = struct.pack('<15I',
        MAGIC, 1, BLOCK_SIZE, TOTAL_BLOCKS, TOTAL_INODES,
        free_blocks, free_inodes,
        INODE_BITMAP_BLOCK, BLOCK_BITMAP_BLOCK,
        INODE_TABLE_BLOCK, INODE_TABLE_BLOCKS,
        DATA_START_BLOCK, ROOT_INODE,
        JOURNAL_START_BLOCK, JOURNAL_BLOCKS)
    return fields + b'\x00' * (BLOCK_SIZE - len(fields))


def pack_inode(mode, size, links, blocks_used, direct, indirect=0, mtime=0,
               uid=0, gid=0, perm=None):
    if perm is None:
        perm = 0o755 if mode == YCFS_TYPE_DIR else 0o644
    direct = (direct + [0] * 12)[:12]
    data = struct.pack('<IQII12IIQIIH', mode, size, links, blocks_used, *direct,
                        indirect, mtime, uid, gid, perm)
    return data + b'\x00' * (INODE_SIZE - len(data))


def pack_dirent(inode, name, file_type):
    nb = name.encode()
    return struct.pack('<IHBB56s', inode, 64, len(nb), file_type, nb.ljust(56, b'\x00'))


def pad_block(data):
    assert len(data) <= BLOCK_SIZE, f"block overflow: {len(data)} > {BLOCK_SIZE}"
    return data + b'\x00' * (BLOCK_SIZE - len(data))


def make_bitmap(used_indices, total_bits):
    nbytes = (total_bits + 7) // 8
    bm = bytearray(nbytes)
    for idx in used_indices:
        bm[idx // 8] |= (1 << (idx % 8))
    return bytes(bm) + b'\x00' * (BLOCK_SIZE - len(bm))


def allocate_indirect_file(content, next_block):
    if not content:
        return [], 0, b"", next_block, 0
    nblocks = (len(content) + BLOCK_SIZE - 1) // BLOCK_SIZE
    blocks = list(range(next_block, next_block + nblocks))
    next_block += nblocks
    direct = blocks[:12]
    overflow = blocks[12:]
    indirect_block = 0
    indirect_ptrs = b""
    if overflow:
        indirect_block = next_block
        next_block += 1
        ptrs_per_block = BLOCK_SIZE // 4
        assert len(overflow) <= ptrs_per_block, \
            f"file too large: needs {len(overflow)} indirect pointers, max {ptrs_per_block}"
        ptrs = overflow + [0] * (ptrs_per_block - len(overflow))
        indirect_ptrs = struct.pack(f'<{ptrs_per_block}I', *ptrs)
    blocks_used = len(direct) + (1 if indirect_block else 0)
    return direct, indirect_block, indirect_ptrs, next_block, blocks_used


def write_indirect_file(f, direct, indirect_block, indirect_ptrs, content):
    offset = 0
    for blk in direct:
        chunk = content[offset:offset+BLOCK_SIZE]
        f.seek(YCFS_START + blk * BLOCK_SIZE)
        f.write(pad_block(chunk))
        offset += BLOCK_SIZE
    if indirect_block:
        ptrs_per_block = BLOCK_SIZE // 4
        overflow_blocks = struct.unpack(f'<{ptrs_per_block}I', indirect_ptrs)
        for blk in overflow_blocks:
            if blk == 0:
                break
            chunk = content[offset:offset+BLOCK_SIZE]
            f.seek(YCFS_START + blk * BLOCK_SIZE)
            f.write(pad_block(chunk))
            offset += BLOCK_SIZE
        f.seek(YCFS_START + indirect_block * BLOCK_SIZE)
        f.write(indirect_ptrs)


def main():
    if len(sys.argv) != 2:
        print("usage: mkycfs.py disk.img")
        sys.exit(1)
    path = sys.argv[1]

    if not os.path.exists(path):
        print(f"error: {path} does not exist (expected an existing FAT16 disk.img)")
        sys.exit(1)

    needed_size = YCFS_START + REGION_SIZE
    cur_size = os.path.getsize(path)
    if cur_size < needed_size:
        with open(path, 'r+b') as f:
            f.seek(needed_size - 1)
            f.write(b'\x00')
        print(f"grew {path} from {cur_size} to {needed_size} bytes")

    hello_content = b"Hello from YCFS!\nThis is Yousuf-Claude File System, phase 1.\n"
    notes_content = b"Nested directories work.\n"
    roottest_content = b"Only root (uid 0) should be able to read this.\n"
    usertest_content  = b"Only the first non-root user (uid 1) should be able to read this.\n"

    wallpaper_path = "wall.bmp"
    wallpaper_content = b""
    if os.path.exists(wallpaper_path):
        with open(wallpaper_path, "rb") as wf:
            wallpaper_content = wf.read()

    ding_content = b""
    if os.path.exists(DING_WAV_PATH):
        with open(DING_WAV_PATH, "rb") as df:
            ding_content = df.read()

    png_content = b""
    if os.path.exists(TEST_PNG_PATH):
        with open(TEST_PNG_PATH, "rb") as pf:
            png_content = pf.read()

    root_block     = DATA_START_BLOCK + 0
    docs_block     = DATA_START_BLOCK + 1
    hello_block    = DATA_START_BLOCK + 2
    notes_block    = DATA_START_BLOCK + 3
    roottest_block = DATA_START_BLOCK + 4
    usertest_block = DATA_START_BLOCK + 5
    next_block     = DATA_START_BLOCK + 6

    wallpaper_direct, wallpaper_indirect, wallpaper_ptrs, next_block, wallpaper_blocks_used = \
        allocate_indirect_file(wallpaper_content, next_block)

    ding_direct, ding_indirect, ding_ptrs, next_block, ding_blocks_used = \
        allocate_indirect_file(ding_content, next_block)

    png_direct, png_indirect, png_ptrs, next_block, png_blocks_used = \
        allocate_indirect_file(png_content, next_block)

    total_used_blocks = next_block

    root_dirents = (pack_dirent(2, "hello.txt", YCFS_TYPE_FILE) +
                     pack_dirent(3, "docs", YCFS_TYPE_DIR) +
                     pack_dirent(6, "roottest.txt", YCFS_TYPE_FILE) +
                     pack_dirent(7, "usertest.txt", YCFS_TYPE_FILE))
    if wallpaper_content:
        root_dirents += pack_dirent(5, "wall.bmp", YCFS_TYPE_FILE)
    if ding_content:
        root_dirents += pack_dirent(8, "ding.wav", YCFS_TYPE_FILE)
    if png_content:
        root_dirents += pack_dirent(9, "test.png", YCFS_TYPE_FILE)
    docs_dirents = pack_dirent(4, "notes.txt", YCFS_TYPE_FILE)

    inode0 = pack_inode(0, 0, 0, 0, [])
    inode1 = pack_inode(YCFS_TYPE_DIR,  len(root_dirents),  1, 1, [root_block])
    inode2 = pack_inode(YCFS_TYPE_FILE, len(hello_content), 1, 1, [hello_block])
    inode3 = pack_inode(YCFS_TYPE_DIR,  len(docs_dirents),  1, 1, [docs_block])
    inode4 = pack_inode(YCFS_TYPE_FILE, len(notes_content), 1, 1, [notes_block])

    inode_table = inode0 + inode1 + inode2 + inode3 + inode4
    used_inodes = [0, 1, 2, 3, 4]

    if wallpaper_content:
        inode5 = pack_inode(YCFS_TYPE_FILE, len(wallpaper_content), 1, wallpaper_blocks_used,
                             wallpaper_direct, indirect=wallpaper_indirect)
        inode_table += inode5
        used_inodes.append(5)
    else:
        inode_table += pack_inode(0, 0, 0, 0, [])

    inode6 = pack_inode(YCFS_TYPE_FILE, len(roottest_content), 1, 1,
                         [roottest_block], uid=0, gid=0, perm=0o600)
    inode7 = pack_inode(YCFS_TYPE_FILE, len(usertest_content), 1, 1,
                         [usertest_block], uid=1, gid=1, perm=0o600)
    inode_table += inode6 + inode7
    used_inodes += [6, 7]

    if ding_content:
        inode8 = pack_inode(YCFS_TYPE_FILE, len(ding_content), 1, ding_blocks_used,
                             ding_direct, indirect=ding_indirect)
        inode_table += inode8
        used_inodes.append(8)
    else:
        inode_table += pack_inode(0, 0, 0, 0, [])

    if png_content:
        inode9 = pack_inode(YCFS_TYPE_FILE, len(png_content), 1, png_blocks_used,
                             png_direct, indirect=png_indirect)
        inode_table += inode9
        used_inodes.append(9)
    else:
        inode_table += pack_inode(0, 0, 0, 0, [])

    inode_table += b'\x00' * (INODE_TABLE_BLOCKS * BLOCK_SIZE - len(inode_table))

    inode_bitmap = make_bitmap(used_inodes, TOTAL_INODES)
    block_bitmap = make_bitmap(list(range(0, total_used_blocks)), TOTAL_BLOCKS)

    free_blocks = TOTAL_BLOCKS - total_used_blocks
    free_inodes = TOTAL_INODES - len(used_inodes)

    superblock = pack_superblock(free_blocks, free_inodes)

    zero_block = b'\x00' * BLOCK_SIZE

    with open(path, 'r+b') as f:
        f.seek(YCFS_START + 0 * BLOCK_SIZE);                 f.write(superblock)
        for jb in range(JOURNAL_START_BLOCK, JOURNAL_START_BLOCK + JOURNAL_BLOCKS):
            f.seek(YCFS_START + jb * BLOCK_SIZE); f.write(zero_block)
        f.seek(YCFS_START + INODE_BITMAP_BLOCK * BLOCK_SIZE); f.write(inode_bitmap)
        f.seek(YCFS_START + BLOCK_BITMAP_BLOCK * BLOCK_SIZE); f.write(block_bitmap)
        f.seek(YCFS_START + INODE_TABLE_BLOCK * BLOCK_SIZE);  f.write(inode_table)
        f.seek(YCFS_START + root_block     * BLOCK_SIZE);     f.write(pad_block(root_dirents))
        f.seek(YCFS_START + docs_block     * BLOCK_SIZE);     f.write(pad_block(docs_dirents))
        f.seek(YCFS_START + hello_block    * BLOCK_SIZE);     f.write(pad_block(hello_content))
        f.seek(YCFS_START + notes_block    * BLOCK_SIZE);     f.write(pad_block(notes_content))
        f.seek(YCFS_START + roottest_block * BLOCK_SIZE);     f.write(pad_block(roottest_content))
        f.seek(YCFS_START + usertest_block * BLOCK_SIZE);     f.write(pad_block(usertest_content))
        if wallpaper_content:
            write_indirect_file(f, wallpaper_direct, wallpaper_indirect, wallpaper_ptrs, wallpaper_content)
        if ding_content:
            write_indirect_file(f, ding_direct, ding_indirect, ding_ptrs, ding_content)
        if png_content:
            write_indirect_file(f, png_direct, png_indirect, png_ptrs, png_content)

    print(f"YCFS formatted: {TOTAL_BLOCKS} blocks ({REGION_SIZE} bytes), {TOTAL_INODES} inodes")
    print(f"  journal: {JOURNAL_BLOCKS} blocks starting at block {JOURNAL_START_BLOCK}")
    print(f"  root (inode 1) -> hello.txt, docs/, roottest.txt, usertest.txt" +
          (", wall.bmp" if wallpaper_content else "") + (", ding.wav" if ding_content else "") +
          (", test.png" if png_content else ""))
    print(f"  hello.txt (inode 2, {len(hello_content)} bytes)")
    print(f"  docs/ (inode 3) -> notes.txt")
    print(f"  notes.txt (inode 4, {len(notes_content)} bytes)")
    if wallpaper_content:
        print(f"  wall.bmp (inode 5, {len(wallpaper_content)} bytes, {wallpaper_blocks_used} blocks)")
    print(f"  roottest.txt (inode 6, uid=0 perm=0600, {len(roottest_content)} bytes)")
    print(f"  usertest.txt (inode 7, uid=1 perm=0600, {len(usertest_content)} bytes)")
    if ding_content:
        print(f"  ding.wav (inode 8, {len(ding_content)} bytes, {ding_blocks_used} blocks)")
    elif not os.path.exists(DING_WAV_PATH):
        print(f"  ding.wav skipped (not found at {DING_WAV_PATH})")
    if png_content:
        print(f"  test.png (inode 9, {len(png_content)} bytes, {png_blocks_used} blocks)")
    elif not os.path.exists(TEST_PNG_PATH):
        print(f"  test.png skipped (not found at {TEST_PNG_PATH})")


if __name__ == '__main__':
    main()
