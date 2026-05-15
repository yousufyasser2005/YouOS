#!/usr/bin/env python3
import struct, sys, os

def build_initrd(files, output):
    MAGIC = 0x59524449
    NAME_MAX = 32
    count = len(files)
    header_size = 8
    entry_size  = NAME_MAX + 8 + 8
    data_start  = header_size + count * entry_size

    entries = []
    datas   = []
    offset  = data_start
    for path in files:
        with open(path, 'rb') as f:
            data = f.read()
        name = os.path.basename(path).encode()[:NAME_MAX-1]
        name = name + b'\x00' * (NAME_MAX - len(name))
        entries.append((name, offset, len(data)))
        datas.append(data)
        offset += len(data)

    with open(output, 'wb') as f:
        f.write(struct.pack('<II', MAGIC, count))
        for name, off, size in entries:
            f.write(name)
            f.write(struct.pack('<QQ', off, size))
        for data in datas:
            f.write(data)

    print(f"initrd: {output} ({offset} bytes, {count} files)")

if __name__ == '__main__':
    build_initrd(sys.argv[2:], sys.argv[1])
