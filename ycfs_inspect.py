#!/usr/bin/env python3
"""
Direct forensic inspection of YCFS on-disk structures in disk.img.
Bypasses the kernel entirely -- reads the raw bytes to see what's
actually persisted, independent of what the kernel code claims to do.

Struct layouts taken verbatim (field-for-field, packed, little-endian)
from include/kernel/ycfs.h.
"""
import struct
import sys

DISK_PATH = "disk.img"
YCFS_START_LBA = 131072
YCFS_START = YCFS_START_LBA * 512  # 67108864, i.e. 64MB into disk.img
BLOCK_SIZE = 4096
INODE_SIZE = 128
DIRENT_SIZE = 64

SB_FMT = "<15I"  # magic..journal_num_blocks, 15 uint32 fields, 60 bytes
SB_FIELDS = [
    "magic", "version", "block_size", "total_blocks", "total_inodes",
    "free_blocks", "free_inodes", "inode_bitmap_block", "block_bitmap_block",
    "inode_table_block", "inode_table_blocks", "data_start_block",
    "root_inode", "journal_start_block", "journal_num_blocks",
]

INODE_FMT = "<IQII12IIQIIH"  # mode,size,links_count,blocks_used,direct[12],indirect,mtime,uid,gid,perm

DIRENT_FMT = "<IHBB56s"  # inode,rec_len,name_len,file_type,name[56]

JOURNAL_HDR_FMT = "<4I"  # magic,txn_id,target_block,type
YCFS_JOURNAL_MAGIC = 0x594A524E  # "YJRN"
YCFS_JTYPE_DESC = 1
YCFS_JTYPE_COMMIT = 2


def read_block(f, block_num):
    f.seek(YCFS_START + block_num * BLOCK_SIZE)
    return f.read(BLOCK_SIZE)


def read_superblock(f):
    data = read_block(f, 0)
    vals = struct.unpack(SB_FMT, data[:60])
    return dict(zip(SB_FIELDS, vals))


def read_inode(f, sb, inode_num):
    inodes_per_block = BLOCK_SIZE // INODE_SIZE
    block = sb["inode_table_block"] + inode_num // inodes_per_block
    idx_in_block = inode_num % inodes_per_block
    data = read_block(f, block)
    raw = data[idx_in_block * INODE_SIZE: idx_in_block * INODE_SIZE + 90]
    vals = struct.unpack(INODE_FMT, raw)
    keys = ["mode", "size", "links_count", "blocks_used"] + \
           [f"direct{i}" for i in range(12)] + \
           ["indirect", "mtime", "uid", "gid", "perm"]
    return dict(zip(keys, vals))


def list_dir(f, sb, dir_inode_num, dir_inode):
    entries = []
    remaining = dir_inode["size"]
    for i in range(12):
        if remaining <= 0:
            break
        block_num = dir_inode[f"direct{i}"]
        if block_num == 0:
            break
        data = read_block(f, block_num)
        n_in_block = min(BLOCK_SIZE, remaining) // DIRENT_SIZE
        for j in range(n_in_block):
            raw = data[j * DIRENT_SIZE:(j + 1) * DIRENT_SIZE]
            inode, rec_len, name_len, file_type, name = struct.unpack(DIRENT_FMT, raw)
            if inode == 0:
                continue
            name_str = name[:name_len].decode("utf-8", errors="replace")
            entries.append({
                "inode": inode, "name": name_str, "file_type": file_type,
                "name_len": name_len, "rec_len": rec_len,
            })
        remaining -= BLOCK_SIZE
    return entries


def dump_journal(f, sb):
    """Mirrors ycfs_journal_replay()'s own scan logic exactly, but only
    READS and reports -- never writes/clears anything. Shows every
    DESC/COMMIT record found, in order, so we can see whether any
    committed transaction is still sitting there un-cleared (which would
    get blindly re-applied at the NEXT boot, potentially stomping a
    later, non-journaled write to the same block)."""
    journal_start = sb["journal_start_block"]
    journal_blocks = sb["journal_num_blocks"]
    print(f"=== Journal region: start_block={journal_start}, num_blocks={journal_blocks} ===")
    if journal_blocks == 0:
        print("  (no journal configured)")
        return

    pos = 0
    found_any = False
    while pos < journal_blocks:
        data = read_block(f, journal_start + pos)
        magic, txn_id, target_block, jtype = struct.unpack(JOURNAL_HDR_FMT, data[:16])
        if magic != YCFS_JOURNAL_MAGIC:
            print(f"  [pos={pos}] no valid journal magic (0x{magic:08x}) -- "
                  f"end of live journal entries, rest is stale/zeroed space.")
            break
        found_any = True
        if jtype == YCFS_JTYPE_DESC:
            print(f"  [pos={pos}] DESC  txn_id={txn_id} target_block={target_block} "
                  f"(data block follows at pos={pos+1})")
            pos += 2
        elif jtype == YCFS_JTYPE_COMMIT:
            print(f"  [pos={pos}] COMMIT txn_id={txn_id}")
            pos += 1
        else:
            print(f"  [pos={pos}] unknown journal record type={jtype} -- stopping scan.")
            break

    if not found_any:
        print("  Journal is fully clean (no valid records at pos=0) -- "
              "nothing pending replay right now.")
    else:
        print()
        print("  ^ If any COMMIT record appears above, that entire transaction")
        print("    will be blindly RE-APPLIED (raw_write_block'd back to its target")
        print("    block) at the very next boot, by ycfs_journal_replay() -- even")
        print("    if a later, non-journaled write already updated that same block")
        print("    with newer/correct data in the meantime.")


def main():
    with open(DISK_PATH, "rb") as f:
        sb = read_superblock(f)
        print("=== Superblock ===")
        for k, v in sb.items():
            print(f"  {k}: {v}")
        print()

        dump_journal(f, sb)
        print()

        root_inode_num = sb["root_inode"]
        root_inode = read_inode(f, sb, root_inode_num)
        root_entries = list_dir(f, sb, root_inode_num, root_inode)
        print("=== Root directory listing ===")
        for e in root_entries:
            print(f"  inode={e['inode']:<4} type={e['file_type']} name={e['name']!r}")
        print()

        lib_entry = next((e for e in root_entries if e["name"] == "lib"), None)
        if not lib_entry:
            print("!!! 'lib' not found in root directory listing -- stopping here.")
            sys.exit(1)

        lib_inode_num = lib_entry["inode"]
        lib_inode = read_inode(f, sb, lib_inode_num)
        lib_entries = list_dir(f, sb, lib_inode_num, lib_inode)
        print("=== lib/ directory listing ===")
        for e in lib_entries:
            print(f"  inode={e['inode']:<4} type={e['file_type']} name={e['name']!r}")
        print()

        # Edit this list to check whichever files matter right now.
        target_names = [e["name"] for e in lib_entries]
        for target_name in target_names:
            target_entry = next((e for e in lib_entries if e["name"] == target_name), None)
            if not target_entry:
                continue
            target_inode_num = target_entry["inode"]
            target_inode = read_inode(f, sb, target_inode_num)
            print(f"=== {target_name} inode (#{target_inode_num}) ===")
            for k, v in target_inode.items():
                print(f"  {k}: {v}")
            block0 = target_inode["direct0"]
            if block0 == 0:
                print("  direct[0] is 0 -- no data block allocated at all!")
            else:
                data = read_block(f, block0)
                declared_size = target_inode["size"]
                print(f"  direct[0] block number: {block0}, inode.size: {declared_size}")
                print(f"  raw bytes (first 64): {data[:64]!r}")
            print()


if __name__ == "__main__":
    main()
