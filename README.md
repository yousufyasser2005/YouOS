# YouOS

A 64-bit operating system built completely from scratch.

> No Linux. No borrowed kernel. Pure YouOS.

## Current Status
- [x] Multiboot2 bootloader
- [x] 32-bit → 64-bit long mode transition
- [x] VGA text mode output
- [ ] GDT / IDT
- [ ] Physical Memory Manager
- [ ] Process Scheduler
- [ ] File System
- [ ] GUI

## Architecture
Target: x86_64  
Bootloader: GRUB (Multiboot2)  
Languages: Assembly (NASM), C, C++

## Building

### Prerequisites
- `x86_64-elf-gcc` cross-compiler
- `nasm`, `meson`, `ninja`, `qemu-system-x86_64`, `grub`

### Build & Run
```bash
meson setup build --cross-file cross_x86_64.ini
cd build && ninja
cd ..
cp build/kernel/youos.elf iso/boot/youos.elf
grub-mkrescue -o build/youos.iso iso/
qemu-system-x86_64 -cdrom build/youos.iso -m 256M -display gtk
```

## License
MIT
