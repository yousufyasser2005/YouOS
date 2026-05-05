; =============================================================================
; YouOS - IDT Interrupt Stubs
; Each CPU exception needs its own stub that:
;   1. Pushes a dummy error code (if CPU doesn't push one)
;   2. Pushes the interrupt number
;   3. Saves all registers
;   4. Calls the common C handler
;   5. Restores registers and returns
; =============================================================================

bits 64

extern idt_common_handler

; Macro for exceptions WITHOUT an error code (CPU doesn't push one)
%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0          ; dummy error code
    push qword %1         ; interrupt number
    jmp  isr_common
%endmacro

; Macro for exceptions WITH an error code (CPU pushes it automatically)
%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1         ; interrupt number (error code already on stack)
    jmp  isr_common
%endmacro

; CPU Exceptions 0-19
ISR_NOERR 0    ; Divide Error
ISR_NOERR 1    ; Debug
ISR_NOERR 2    ; NMI
ISR_NOERR 3    ; Breakpoint
ISR_NOERR 4    ; Overflow
ISR_NOERR 5    ; Bound Range Exceeded
ISR_NOERR 6    ; Invalid Opcode
ISR_NOERR 7    ; Device Not Available
ISR_ERR   8    ; Double Fault        (has error code)
ISR_NOERR 9    ; Coprocessor Segment (reserved)
ISR_ERR   10   ; Invalid TSS         (has error code)
ISR_ERR   11   ; Segment Not Present (has error code)
ISR_ERR   12   ; Stack Fault         (has error code)
ISR_ERR   13   ; General Protection  (has error code)
ISR_ERR   14   ; Page Fault          (has error code)
ISR_NOERR 15   ; Reserved
ISR_NOERR 16   ; FPU Error
ISR_ERR   17   ; Alignment Check     (has error code)
ISR_NOERR 18   ; Machine Check
ISR_NOERR 19   ; SIMD FP Exception

; =============================================================================
; Common handler — saves all registers, calls C, restores
; =============================================================================
isr_common:
    ; Save general purpose registers
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

    ; Pass pointer to register frame as first argument (rdi = registers_t*)
    mov rdi, rsp
    call idt_common_handler

    ; Restore registers
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

    ; Clean up int_no and err_code
    add rsp, 16

    iretq
