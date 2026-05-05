#!/bin/bash
set -e
ROOT="/home/yousuf/codes/YouOS"
cd "$ROOT"

echo "[1/3] Compiling kernel..."
cd build && ninja
cd ..

echo "[2/3] Creating ISO..."
cp build/youos.elf iso/boot/youos.elf
grub-mkrescue -o build/youos.iso iso/

echo "[3/3] Done! → build/youos.iso"
