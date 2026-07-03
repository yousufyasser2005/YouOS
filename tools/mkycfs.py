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
DATA_START_BLOCK = INODE_TABLE_BLOCK + INODE_TABLE_BLOCKS
ROOT_INODE = 1


def pack_superblock(free_blocks, free_inodes):
    fields = struct.pack('<13I',
        MAGIC, 1, BLOCK_SIZE, TOTAL_BLOCKS, TOTAL_INODES,
        free_blocks, free_inodes,
        INODE_BITMAP_BLOCK, BLOCK_BITMAP_BLOCK,
        INODE_TABLE_BLOCK, INODE_TABLE_BLOCKS,
        DATA_START_BLOCK, ROOT_INODE)
    return fields + b'\x00' * (BLOCK_SIZE - len(fields))


def pack_inode(mode, size, links, blocks_used, direct, indirect=0, mtime=0):
    direct = (direct + [0] * 12)[:12]
    data = struct.pack('<IQII12IIQ', mode, size, links, blocks_used, *direct, indirect, mtime)
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

    root_block  = DATA_START_BLOCK + 0
    docs_block  = DATA_START_BLOCK + 1
    hello_block = DATA_START_BLOCK + 2
    notes_block = DATA_START_BLOCK + 3
    total_used_blocks = DATA_START_BLOCK + 4

    root_dirents = pack_dirent(2, "hello.txt", YCFS_TYPE_FILE) + pack_dirent(3, "docs", YCFS_TYPE_DIR)
    docs_dirents = pack_dirent(4, "notes.txt", YCFS_TYPE_FILE)

    inode0 = pack_inode(0, 0, 0, 0, [])
    inode1 = pack_inode(YCFS_TYPE_DIR,  len(root_dirents),  1, 1, [root_block])
    inode2 = pack_inode(YCFS_TYPE_FILE, len(hello_content), 1, 1, [hello_block])
    inode3 = pack_inode(YCFS_TYPE_DIR,  len(docs_dirents),  1, 1, [docs_block])
    inode4 = pack_inode(YCFS_TYPE_FILE, len(notes_content), 1, 1, [notes_block])

    inode_table = inode0 + inode1 + inode2 + inode3 + inode4
    inode_table += b'\x00' * (INODE_TABLE_BLOCKS * BLOCK_SIZE - len(inode_table))

    inode_bitmap = make_bitmap([0, 1, 2, 3, 4], TOTAL_INODES)
    block_bitmap = make_bitmap(list(range(0, total_used_blocks)), TOTAL_BLOCKS)

    free_blocks = TOTAL_BLOCKS - total_used_blocks
    free_inodes = TOTAL_INODES - 5

    superblock = pack_superblock(free_blocks, free_inodes)

    with open(path, 'r+b') as f:
        f.seek(YCFS_START + 0 * BLOCK_SIZE);                 f.write(superblock)
        f.seek(YCFS_START + INODE_BITMAP_BLOCK * BLOCK_SIZE); f.write(inode_bitmap)
        f.seek(YCFS_START + BLOCK_BITMAP_BLOCK * BLOCK_SIZE); f.write(block_bitmap)
        f.seek(YCFS_START + INODE_TABLE_BLOCK * BLOCK_SIZE);  f.write(inode_table)
        f.seek(YCFS_START + root_block  * BLOCK_SIZE);        f.write(pad_block(root_dirents))
        f.seek(YCFS_START + docs_block  * BLOCK_SIZE);        f.write(pad_block(docs_dirents))
        f.seek(YCFS_START + hello_block * BLOCK_SIZE);        f.write(pad_block(hello_content))
        f.seek(YCFS_START + notes_block * BLOCK_SIZE);        f.write(pad_block(notes_content))

    print(f"YCFS formatted: {TOTAL_BLOCKS} blocks ({REGION_SIZE} bytes), {TOTAL_INODES} inodes")
    print(f"  root (inode 1) -> hello.txt, docs/")
    print(f"  hello.txt (inode 2, {len(hello_content)} bytes)")
    print(f"  docs/ (inode 3) -> notes.txt")
    print(f"  notes.txt (inode 4, {len(notes_content)} bytes)")


if __name__ == '__main__':
    main()
