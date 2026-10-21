#!/usr/bin/env python3
"""
mkycfs.py -- FORMAT the YCFS region inside disk.img and seed a small test tree.

    python3 tools/mkycfs.py disk.img [--size 4G] [--yes]

*** This ERASES everything in /ycfs: accounts, settings, wallpaper, every file. ***
To add or replace files on an existing image WITHOUT losing anything use
tools/ycfs.py instead:

    python3 tools/ycfs.py put  disk.img song.mp3 /music/
    python3 tools/ycfs.py ls   disk.img -R

To move an existing (old, 16MB) image to the new big layout KEEPING all files:

    python3 tools/ycfs.py migrate disk.img --size 4G

Shut QEMU down before running any of these.

Layout: the first 64MB of disk.img belong to FAT16 and are never touched.
YCFS starts at 64MB and runs to the end of the image, so its size is whatever
--size makes the image (default: 4G).  The file is grown sparsely, so a 4G
image costs almost no real disk space until you fill it.

Seeds (same as the old tool) /hello.txt, /docs/notes.txt, /hello.py,
/lib/testmod.py and the two permission-test files, plus -- if they exist on
this host -- wall.bmp (from the current directory) and the media test files
named below.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ycfs  # noqa: E402

DING_WAV_PATH = "/home/yousuf/codes/os/assets/sounds/ding.wav"
TEST_PNG_PATH = "/home/yousuf/Pictures/Screenshots/Screenshot from 2026-01-13 10-28-37.png"
TEST_JPG_PATH = "/home/yousuf/Pictures/Screenshots/test_for_youos.jpg"
TEST_YMJP_PATH = "/home/yousuf/Pictures/Screenshots/test_for_youos.ymjp"
DEFAULT_SIZE = "4G"


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="FORMAT the YCFS region of disk.img (erases /ycfs)")
    ap.add_argument("img")
    ap.add_argument("--size", default=DEFAULT_SIZE, help="total image size (default %s)" % DEFAULT_SIZE)
    ap.add_argument("--yes", action="store_true", help="confirm erasing an existing YCFS")
    a = ap.parse_args(argv[1:])
    path, size, yes = a.img, a.size, a.yes

    try:
        if not os.path.exists(path):
            print("error: %s does not exist (expected an existing FAT16 disk.img)" % path)
            return 1
        try:
            ycfs.Image(path).close()
            existing = True
        except ycfs.YcfsError:
            existing = False
        if existing and not yes:
            print("error: %s already contains a YCFS. Formatting would ERASE all of it\n"
                  "(accounts, settings, wallpaper, files).\n\n"
                  "  keep your files, add one:   python3 tools/ycfs.py put %s FILE /dest/\n"
                  "  keep your files, resize:    python3 tools/ycfs.py migrate %s --size %s\n"
                  "  really erase and reformat:  re-run with --yes" % (path, path, path, size))
            return 1

        target = ycfs.parse_size(size)
        cur = os.path.getsize(path)
        g = ycfs.mkfs(path, max(target, cur) if target >= cur else None)
        print("YCFS formatted: %s, %d inodes" % (ycfs.human(g["total_blocks"] * ycfs.BLOCK), g["total_inodes"]))

        img = ycfs.Image(path, write=True)
        try:
            img.put_bytes("/hello.txt", b"Hello from YCFS!\nThis is Yousuf-Claude File System, phase 1.\n")
            img.mkdir("/docs")
            img.put_bytes("/docs/notes.txt", b"Nested directories work.\n")
            img.put_bytes("/roottest.txt", b"Only root (uid 0) should be able to read this.\n", perm=0o600)
            img.put_bytes("/usertest.txt",
                          b"Only the first non-root user (uid 1) should be able to read this.\n",
                          uid=1, gid=1, perm=0o600)
            img.put_bytes("/hello.py", b"print('file I/O works!')\nfor i in range(3):\n    print(i * 10)\n")
            img.mkdir("/lib")
            img.put_bytes("/lib/testmod.py",
                          b"GREETING = 'hello from testmod'\n\ndef add(a, b):\n    return a + b\n")
            for src, dest in (("wall.bmp", "/wall.bmp"), (DING_WAV_PATH, "/ding.wav"),
                              (TEST_PNG_PATH, "/test.png"), (TEST_JPG_PATH, "/test.jpg"),
                              (TEST_YMJP_PATH, "/test.ymjp")):
                if os.path.exists(src):
                    with open(src, "rb") as f:
                        data = f.read()
                    img.put_bytes(dest, data)
                    print("  seeded %s (%s)" % (dest, ycfs.human(len(data))))
                else:
                    print("  skipped %s (not found: %s)" % (dest, src))
        finally:
            img.close()
        img = ycfs.Image(path)
        problems = ycfs.fsck(img)
        img.close()
        return 1 if problems else 0
    except ycfs.YcfsError as e:
        print("mkycfs: error:", e)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
