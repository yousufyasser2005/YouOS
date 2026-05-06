; =============================================================================
; YouOS - IRQ Stubs (Hardware Interrupts 32-47)
; =============================================================================

bits 64

extern irq_common_handler

%macro IRQ_STUB 1
global irq%1
irq%1:
    push qword 0        ; no error code
    push qword %1       ; IRQ number
    jmp  irq_common
%endmacro

IRQ_STUB 0    ; Timer
IRQ_STUB 1    ; Keyboard
IRQ_STUB 2    ; Cascade
IRQ_STUB 3    ; COM2
IRQ_STUB 4    ; COM1
IRQ_STUB 5    ; LPT2
IRQ_STUB 6    ; Floppy
IRQ_STUB 7    ; LPT1
IRQ_STUB 8    ; RTC
IRQ_STUB 9    ; Free
IRQ_STUB 10   ; Free
IRQ_STUB 11   ; Free
IRQ_STUB 12   ; Mouse
IRQ_STUB 13   ; FPU
IRQ_STUB 14   ; ATA1
IRQ_STUB 15   ; ATA2

irq_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp            ; pass registers_t* as first arg
    call irq_common_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16             ; clean up irq_no + err_code
    iretq
