section .initrd
global _initrd_start
global _initrd_end

_initrd_start:
    incbin "/home/yousuf/codes/YouOS/iso/boot/initrd.img"
_initrd_end:
