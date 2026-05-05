; =============================================================================
; YouOS - GDT Flush
; =============================================================================

bits 64

global gdt_flush
global tss_flush

; void gdt_flush(uint64_t gdt_ptr)
; rdi = pointer to gdt_ptr_t
gdt_flush:
    lgdt [rdi]

    ; We need to reload CS with a far jump
    ; Push new CS and return address onto stack, then retfq
    mov  rax, .reload
    push 0x08               ; kernel code selector
    push rax
    retfq

.reload:
    mov ax, 0x10            ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

; void tss_flush(void)
tss_flush:
    mov ax, 0x28
    ltr ax
    ret
