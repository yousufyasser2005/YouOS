#!/bin/bash
set -e
ROOT="/home/yousuf/codes/YouOS"
ISO="$ROOT/build/youos.iso"

if [ ! -f "$ISO" ]; then
    echo "ISO not found. Run ./tools/scripts/build.sh first."
    exit 1
fi

qemu-system-x86_64 \
  -cdrom "$ISO" \
  -m 256M \
  -serial stdio \
  -no-reboot \
  -no-shutdown \
  "$@"
