ENTRY(_start)

SECTIONS {
    . = 0x400000;

    .text : {
        *(.text._start)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    /* Page-align before .data so the linker places .text/.rodata (code,
     * no SHF_WRITE) and .data/.bss (writable, no SHF_EXECINSTR) into
     * separate PT_LOAD program headers instead of merging everything
     * into one combined segment. Without this, every binary built with
     * this script could end up with a single LOAD segment carrying the
     * union of both sections' flags -- readable, writable, AND
     * executable at once -- which is what a prior session's handoff
     * flagged specifically on bin/mpy (MicroPython's much larger
     * binary made it most visible, though the same risk applied to
     * every binary linked with this script).
     *
     * NOTE: this only fixes the ELF-level segment split. The kernel's
     * own ELF loader (kernel/fs/elf.c) does not yet set an NX bit on
     * non-executable segments -- doing so requires enabling EFER.NXE in
     * boot.asm first (currently only LME is set) and is a separate,
     * larger, higher-risk change than this linker script fix. Until
     * that lands, .data/.bss pages are still hardware-executable
     * regardless of this segment split; this change alone resolves the
     * linker's own "RWX LOAD segment" warning by giving it the
     * information needed to emit correctly-scoped segments, not full
     * W^X enforcement. */
    . = ALIGN(4096);

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
