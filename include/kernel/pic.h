#ifndef KERNEL_PIC_H
#define KERNEL_PIC_H

#include <stdint.h>

/*
 * PIC I/O ports
 */
#define PIC1_CMD    0x20    /* Master PIC command port */
#define PIC1_DATA   0x21    /* Master PIC data port    */
#define PIC2_CMD    0xA0    /* Slave PIC command port  */
#define PIC2_DATA   0xA1    /* Slave PIC data port     */

/*
 * PIC commands
 */
#define PIC_EOI     0x20    /* End of Interrupt signal */
#define PIC_INIT    0x11    /* Initialize PIC          */

/*
 * IRQ vector offsets
 * We remap IRQs to 0x20-0x2F to avoid conflicts with CPU exceptions
 */
#define PIC1_OFFSET 0x20    /* IRQ 0-7  → vectors 32-39  */
#define PIC2_OFFSET 0x28    /* IRQ 8-15 → vectors 40-47  */

/*
 * IRQ numbers
 */
#define IRQ_TIMER       0
#define IRQ_KEYBOARD    1
#define IRQ_CASCADE     2
#define IRQ_COM2        3
#define IRQ_COM1        4
#define IRQ_LPT2        5
#define IRQ_FLOPPY      6
#define IRQ_LPT1        7
#define IRQ_RTC         8
#define IRQ_MOUSE       12
#define IRQ_FPU         13
#define IRQ_ATA1        14
#define IRQ_ATA2        15

/* Initialize and remap the PIC */
void pic_init(void);

/* Send End of Interrupt to PIC */
void pic_eoi(uint8_t irq);

/* Mask (disable) an IRQ line */
void pic_mask(uint8_t irq);

/* Unmask (enable) an IRQ line */
void pic_unmask(uint8_t irq);

/* Disable PIC entirely (for APIC use later) */
void pic_disable(void);

#endif /* KERNEL_PIC_H */
