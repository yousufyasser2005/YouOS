/*
 * YouOS - Programmable Interrupt Controller (8259 PIC)
 * pic.c
 *
 * The PC has two cascaded 8259 PICs:
 *   Master (PIC1): handles IRQ 0-7
 *   Slave  (PIC2): handles IRQ 8-15, connected to Master IRQ2
 *
 * By default IRQ 0-7 map to INT 8-15 which conflicts with CPU exceptions.
 * We remap them to INT 32-47 (0x20-0x2F).
 */

#include <kernel/pic.h>

/* =========================================================================
 * Port I/O helpers
 * ========================================================================= */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Small I/O delay — needed between PIC commands on old hardware */
static inline void io_wait(void) {
    outb(0x80, 0);
}

/* =========================================================================
 * pic_init — remap and initialize both PICs
 * ========================================================================= */
void pic_init(void)
{
    /* Save current masks */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* Start initialization sequence (ICW1) */
    outb(PIC1_CMD,  PIC_INIT); io_wait();
    outb(PIC2_CMD,  PIC_INIT); io_wait();

    /* ICW2: Set vector offsets */
    outb(PIC1_DATA, PIC1_OFFSET); io_wait();   /* Master: IRQ0 → INT 32 */
    outb(PIC2_DATA, PIC2_OFFSET); io_wait();   /* Slave:  IRQ8 → INT 40 */

    /* ICW3: Tell Master about Slave on IRQ2, tell Slave its cascade ID */
    outb(PIC1_DATA, 0x04); io_wait();  /* Master: Slave on IRQ2 (bit 2) */
    outb(PIC2_DATA, 0x02); io_wait();  /* Slave:  cascade identity = 2  */

    /* ICW4: Set 8086 mode */
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    /* Restore saved masks */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

/* =========================================================================
 * pic_eoi — send End of Interrupt
 * Must be called at the end of every IRQ handler
 * ========================================================================= */
void pic_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);   /* Also send to slave for IRQ 8-15 */
    outb(PIC1_CMD, PIC_EOI);
}

/* =========================================================================
 * pic_mask / pic_unmask
 * ========================================================================= */
void pic_mask(uint8_t irq)
{
    uint16_t port;
    uint8_t  val;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    val = inb(port) | (1 << irq);
    outb(port, val);
}

void pic_unmask(uint8_t irq)
{
    uint16_t port;
    uint8_t  val;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    val = inb(port) & ~(1 << irq);
    outb(port, val);
}

/* =========================================================================
 * pic_disable — mask all IRQs (used when switching to APIC later)
 * ========================================================================= */
void pic_disable(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
