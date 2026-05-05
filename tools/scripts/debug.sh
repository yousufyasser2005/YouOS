#!/bin/bash
set -e
ROOT="/home/yousuf/codes/YouOS"

echo "Starting QEMU paused, waiting for GDB on port 1234..."
qemu-system-x86_64 \
  -cdrom "$ROOT/build/youos.iso" \
  -m 256M \
  -serial stdio \
  -no-reboot \
  -no-shutdown \
  -s -S &

echo ""
echo "Now run in another terminal:"
echo "  gdb $ROOT/build/youos.elf"
echo "  (gdb) target remote :1234"
echo "  (gdb) continue"
