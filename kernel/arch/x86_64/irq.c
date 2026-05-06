/*
 * YouOS - IRQ Handler
 * irq.c
 */

#include <stdint.h>
#include <kernel/pic.h>
#include <kernel/idt.h>

/* Tick counter — incremented by timer IRQ */
static volatile uint64_t ticks = 0;

/* Custom IRQ handler table */
static void (*irq_handlers[16])(registers_t*);

/* Forward declarations */
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static void (*irq_stubs[])(void) = {
    irq0,  irq1,  irq2,  irq3,
    irq4,  irq5,  irq6,  irq7,
    irq8,  irq9,  irq10, irq11,
    irq12, irq13, irq14, irq15,
};

/* Called by all IRQ stubs */
void irq_common_handler(registers_t* regs)
{
    uint8_t irq = (uint8_t)regs->int_no;

    /* Timer tick */
    if (irq == IRQ_TIMER) ticks++;

    /* Call custom handler if registered */
    if (irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq](regs);
    }

    /* Send EOI to PIC */
    pic_eoi(irq);
}

/* Register IRQ stubs in the IDT and init PIC */
void irq_init(void)
{
    /* Zero handler table */
    for (int i = 0; i < 16; i++) irq_handlers[i] = 0;

    /* Install IRQ stubs into IDT at vectors 32-47 */
    for (int i = 0; i < 16; i++) {
        uint64_t addr     = (uint64_t)irq_stubs[i];
        idt_entry_t entry;
        entry.offset_low  = addr & 0xFFFF;
        entry.selector    = 0x08;
        entry.ist         = 0;
        entry.type_attr   = 0x8E;   /* Present, Ring0, Interrupt Gate */
        entry.offset_mid  = (addr >> 16) & 0xFFFF;
        entry.offset_high = (addr >> 32) & 0xFFFFFFFF;
        entry.reserved    = 0;

        extern idt_entry_t idt[];
        idt[32 + i] = entry;
    }

    /* Remap and init PIC */
    pic_init();

    /* Mask all IRQs except timer (IRQ0) for now */
    pic_mask(IRQ_KEYBOARD);
    pic_mask(IRQ_CASCADE);
    pic_mask(IRQ_COM2);
    pic_mask(IRQ_COM1);
    pic_mask(IRQ_LPT2);
    pic_mask(IRQ_FLOPPY);
    pic_mask(IRQ_LPT1);
    pic_mask(IRQ_RTC);
    pic_mask(9); pic_mask(10); pic_mask(11);
    pic_mask(IRQ_MOUSE);
    pic_mask(IRQ_FPU);
    pic_mask(IRQ_ATA1);
    pic_mask(IRQ_ATA2);
}

/* Register a custom handler for an IRQ */
void irq_set_handler(uint8_t irq, void (*handler)(registers_t*))
{
    if (irq < 16) irq_handlers[irq] = handler;
}

/* Get current tick count */
uint64_t irq_get_ticks(void) { return ticks; }
