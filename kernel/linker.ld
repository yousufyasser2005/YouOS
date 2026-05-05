OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS {
    . = 1M;

    /* Multiboot2 header MUST be first and within first 32KB */
    .multiboot2 : {
        KEEP(*(.multiboot2))
    }

    .text ALIGN(4K) : {
        *(.text*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    .data ALIGN(4K) : {
        *(.data*)
    }

    .bss ALIGN(4K) : {
        _bss_start = .;
        *(COMMON)
        *(.bss*)
        _bss_end = .;
    }

    _kernel_end = .;
}
