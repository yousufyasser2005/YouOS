#!/usr/bin/env bash
# Renders the file-transfer progress window (real desktop.c drawing code, stubbed syscalls)
# to PNGs so layout can be checked without booting QEMU.   bash tests/host/render_transfer.sh
# Needs: gcc, python3 with Pillow (pip install pillow). Output: /tmp/youos_render/*.png
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd); W=/tmp/youos_render; rm -rf $W; mkdir -p $W/user/lib $W/user/src
cp "$ROOT"/user/src/*.h "$ROOT"/user/src/desktop.c $W/user/src/
python3 - "$ROOT" "$W" << 'PY'
import sys
root, w = sys.argv[1:3]
s = open(root + "/user/lib/syscall.h").read()
i = s.index("static inline uint64_t _sc("); j = s.index("}\n", s.index("return r;", i)) + 2
s = s[:i] + ("uint64_t host_sc(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e);\n"
             "static inline uint64_t _sc(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e)"
             "{return host_sc(n,a,b,c,d,e);}\n") + s[j:]
open(w + "/user/lib/syscall.h", "w").write(s)
PY
sed "s#/tmp/rh/#$W/#g" "$ROOT/tests/host/render_transfer.c" > $W/render.c
gcc -g -w -O0 -I $W -o $W/render $W/render.c && $W/render
for n in copying scanning error start deleting deleting_big delete_failed; do python3 -c "from PIL import Image; Image.open('$W/$n.ppm').save('$W/$n.png')"; done
echo "wrote $W/{copying,scanning,error,start,deleting,deleting_big,delete_failed}.png"
