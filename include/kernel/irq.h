#ifndef KERNEL_IRQ_H
#define KERNEL_IRQ_H

#include <stdint.h>
#include <kernel/idt.h>

void     irq_init(void);
void     irq_set_handler(uint8_t irq, void (*handler)(registers_t*));
uint64_t irq_get_ticks(void);

#endif
