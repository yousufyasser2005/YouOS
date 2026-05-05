/*
 * YouOS - Interrupt Descriptor Table
 * idt.c
 */

#include <kernel/idt.h>
#include <kernel/vga.h>

/* IDT with 256 entries (32 exceptions + 224 IRQ/syscall slots) */
static idt_entry_t idt[256];
static idt_ptr_t   idt_ptr;

/* Custom handler table — one per vector */
static void (*handlers[256])(registers_t*);

/* Exception names for panic messages */
static const char* exception_names[] = {
    "Division Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
};

/* Forward declarations for all stubs */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void);

/* ISR function pointer array for easy iteration */
static void (*isr_stubs[])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,
    isr6,  isr7,  isr8,  isr9,  isr10, isr11,
    isr12, isr13, isr14, isr15, isr16, isr17,
    isr18, isr19,
};

/* Build one IDT entry */
static idt_entry_t idt_make_entry(void (*handler)(void),
                                   uint16_t selector,
                                   uint8_t  type_attr)
{
    idt_entry_t e;
    uint64_t addr  = (uint64_t)handler;
    e.offset_low   = addr & 0xFFFF;
    e.selector     = selector;
    e.ist          = 0;
    e.type_attr    = type_attr;
    e.offset_mid   = (addr >> 16) & 0xFFFF;
    e.offset_high  = (addr >> 32) & 0xFFFFFFFF;
    e.reserved     = 0;
    return e;
}

extern void idt_load(uint64_t ptr);

/* ==========================================================================
 * Common C handler — called by all stubs
 * ========================================================================== */
void idt_common_handler(registers_t* regs)
{
    /* If a custom handler is registered, call it */
    if (handlers[regs->int_no]) {
        handlers[regs->int_no](regs);
        return;
    }

    /* Default: kernel panic for unhandled exceptions */
    if (regs->int_no < 20) {
        vga_puts_color("\n  [PANIC] CPU Exception: ", VGA_LIGHT_RED, VGA_BLACK);
        vga_puts_color(exception_names[regs->int_no], VGA_WHITE, VGA_BLACK);

        /* Show error code for exceptions that have one */
        if (regs->int_no == 14) {
            /* Page fault — CR2 holds the faulting address */
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            vga_puts_color("\n  Fault address: 0x", VGA_YELLOW, VGA_BLACK);
            /* Print CR2 in hex */
            char hex[17];
            uint64_t val = cr2;
            for (int i = 15; i >= 0; i--) {
                hex[i] = "0123456789ABCDEF"[val & 0xF];
                val >>= 4;
            }
            hex[16] = 0;
            vga_puts_color(hex, VGA_YELLOW, VGA_BLACK);
        }

        vga_puts_color("\n  System Halted.\n", VGA_LIGHT_RED, VGA_BLACK);
    }

    /* Halt */
    __asm__ volatile ("cli; hlt");
}

/* ==========================================================================
 * Public API
 * ========================================================================== */
void idt_init(void)
{
    /* Zero out IDT and handler table */
    for (int i = 0; i < 256; i++) {
        idt[i]      = idt_make_entry(0, 0, 0);
        handlers[i] = 0;
    }

    /* Install exception stubs 0-19 */
    for (int i = 0; i < 20; i++) {
        idt[i] = idt_make_entry(isr_stubs[i], 0x08, IDT_INTERRUPT_GATE);
    }

    /* Load IDT */
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)&idt;
    idt_load((uint64_t)&idt_ptr);
}

void idt_set_handler(uint8_t vector, void (*handler)(registers_t*))
{
    handlers[vector] = handler;
}
