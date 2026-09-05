bits 64
global jump_to_userspace

USER_CODE equ 0x1B
USER_DATA equ 0x23

jump_to_userspace:
    cli
    push USER_DATA
    push rsi
    push qword 0x202    ; IF=1, interrupts ENABLED in Ring 3 -- Phase 2.
                         ; Bit 1 (0x002) is the always-1 reserved RFLAGS
                         ; bit, must stay set. Bit 9 (0x200) is IF.
                         ; Previously hardcoded 0x002 (IF=0), meaning the
                         ; timer could never preempt running ring-3 code,
                         ; confirmed empirically: a pure ring-3 busy loop
                         ; with no syscalls showed exactly 0 ticks elapsed
                         ; regardless of real wall-clock time spent in it.
                         ; TSS.RSP0 is already proven safe for ring3->ring0
                         ; privilege transitions via this exact mechanism
                         ; (the crash.c fault-recovery path already
                         ; exercises it for exceptions, which use the same
                         ; IDT-gate stack-switch hardware as maskable
                         ; interrupts) -- this change only adds a new
                         ; TRIGGER (the timer) for an already-working path.
    push USER_CODE
    push rdi
    mov ax, USER_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iretq
