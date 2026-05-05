#ifndef KERNEL_IDT_H
#define KERNEL_IDT_H

#include <stdint.h>

/*
 * IDT Gate Types
 */
#define IDT_INTERRUPT_GATE  0x8E    /* Present, Ring 0, Interrupt Gate */
#define IDT_TRAP_GATE       0x8F    /* Present, Ring 0, Trap Gate      */
#define IDT_USER_GATE       0xEE    /* Present, Ring 3, Interrupt Gate */

/*
 * CPU Exception Numbers
 */
#define EXC_DIVIDE_ERROR        0
#define EXC_DEBUG               1
#define EXC_NMI                 2
#define EXC_BREAKPOINT          3
#define EXC_OVERFLOW            4
#define EXC_BOUND_RANGE         5
#define EXC_INVALID_OPCODE      6
#define EXC_DEVICE_NA           7
#define EXC_DOUBLE_FAULT        8
#define EXC_INVALID_TSS         10
#define EXC_SEGMENT_NP          11
#define EXC_STACK_FAULT         12
#define EXC_GENERAL_PROTECTION  13
#define EXC_PAGE_FAULT          14
#define EXC_FPU_ERROR           16
#define EXC_ALIGNMENT_CHECK     17
#define EXC_MACHINE_CHECK       18
#define EXC_SIMD_ERROR          19

/*
 * IDT Entry (16 bytes)
 */
typedef struct {
    uint16_t offset_low;      /* Handler address bits 0-15   */
    uint16_t selector;        /* Code segment selector        */
    uint8_t  ist;             /* Interrupt Stack Table index  */
    uint8_t  type_attr;       /* Type and attributes          */
    uint16_t offset_mid;      /* Handler address bits 16-31  */
    uint32_t offset_high;     /* Handler address bits 32-63  */
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

/*
 * IDT Pointer
 */
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

/*
 * Registers pushed by our interrupt stubs
 */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no;    /* Interrupt number */
    uint64_t err_code;  /* Error code (0 if none) */
    /* CPU auto-pushes these on interrupt: */
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) registers_t;

/* Initialize IDT */
void idt_init(void);

/* Register a custom handler for an interrupt */
void idt_set_handler(uint8_t vector, void (*handler)(registers_t*));

#endif /* KERNEL_IDT_H */
