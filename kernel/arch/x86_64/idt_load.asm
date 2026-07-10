bits 64
global idt_load

; void idt_load(uint64_t idt_ptr)
; rdi = pointer to idt_ptr_t
idt_load:
    lidt [rdi]
    ; NOTE: interrupts are intentionally NOT enabled here. The PIC is
    ; still at its default vector offset (0x08-0x0F) until irq_init()
    ; remaps it — enabling interrupts before that remap lets a routine
    ; hardware IRQ (e.g. the timer) land on vector 0x08, which is also
    ; the CPU's Double Fault vector, and get misreported as a crash.
    ; See kernel_main.c, where interrupts are enabled explicitly via
    ; sti_enable() after irq_init() completes.
    ret

global sti_enable
sti_enable:
    sti
    ret
