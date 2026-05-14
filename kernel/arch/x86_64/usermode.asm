bits 64
global jump_to_userspace

USER_CODE equ 0x1B
USER_DATA equ 0x23

jump_to_userspace:
    cli
    push USER_DATA
    push rsi
    push qword 0x002    ; IF=0, interrupts disabled in Ring 3 for now
    push USER_CODE
    push rdi
    mov ax, USER_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iretq
