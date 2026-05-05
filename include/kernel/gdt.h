#ifndef KERNEL_GDT_H
#define KERNEL_GDT_H

#include <stdint.h>

/*
 * GDT Segment Selectors
 * These are the byte offsets into the GDT table.
 * Used when loading segment registers.
 */
#define GDT_KERNEL_CODE   0x08
#define GDT_KERNEL_DATA   0x10
#define GDT_USER_CODE     0x18
#define GDT_USER_DATA     0x20
#define GDT_TSS           0x28

/*
 * GDT Entry (8 bytes)
 * Represents one segment descriptor.
 */
typedef struct {
    uint16_t limit_low;       /* Segment limit bits 0-15  */
    uint16_t base_low;        /* Base address bits 0-15   */
    uint8_t  base_mid;        /* Base address bits 16-23  */
    uint8_t  access;          /* Access flags             */
    uint8_t  granularity;     /* Granularity + limit 16-19 */
    uint8_t  base_high;       /* Base address bits 24-31  */
} __attribute__((packed)) gdt_entry_t;

/*
 * TSS Entry (16 bytes in 64-bit mode)
 * The TSS descriptor is twice as wide as a normal descriptor.
 */
typedef struct {
    gdt_entry_t low;
    uint32_t    base_upper;   /* Base address bits 32-63  */
    uint32_t    reserved;
} __attribute__((packed)) gdt_tss_entry_t;

/*
 * GDT Pointer
 * Loaded into the CPU via lgdt instruction.
 */
typedef struct {
    uint16_t limit;           /* Size of GDT - 1          */
    uint64_t base;            /* Address of GDT           */
} __attribute__((packed)) gdt_ptr_t;

/*
 * Task State Segment (64-bit)
 * Used for kernel stack switching on interrupts from userspace.
 */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;            /* Kernel stack pointer (Ring 0) */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];          /* Interrupt Stack Table */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

/* Initialize the GDT */
void gdt_init(void);

/* Set the kernel stack in the TSS (called on every context switch) */
void tss_set_kernel_stack(uint64_t stack);

#endif /* KERNEL_GDT_H */
