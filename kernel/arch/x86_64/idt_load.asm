bits 64
global idt_load

; void idt_load(uint64_t idt_ptr)
; rdi = pointer to idt_ptr_t
idt_load:
    lidt [rdi]
    sti                 ; Enable interrupts
    ret
