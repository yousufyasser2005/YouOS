#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <stdint.h>
#include <kernel/vmm.h>

/* ELF64 types */
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;

/* ELF magic */
#define ELF_MAGIC0  0x7F
#define ELF_MAGIC1  'E'
#define ELF_MAGIC2  'L'
#define ELF_MAGIC3  'F'

/* ELF class */
#define ELFCLASS64  2

/* ELF type */
#define ET_EXEC     2

/* Program header types */
#define PT_LOAD     1

/* Program header flags */
#define PF_X        1   /* Execute */
#define PF_W        2   /* Write */
#define PF_R        4   /* Read */

/* ELF64 header */
typedef struct {
    uint8_t     e_ident[16];
    Elf64_Half  e_type;
    Elf64_Half  e_machine;
    Elf64_Word  e_version;
    Elf64_Addr  e_entry;
    Elf64_Off   e_phoff;
    Elf64_Off   e_shoff;
    Elf64_Word  e_flags;
    Elf64_Half  e_ehsize;
    Elf64_Half  e_phentsize;
    Elf64_Half  e_phnum;
    Elf64_Half  e_shentsize;
    Elf64_Half  e_shnum;
    Elf64_Half  e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

/* ELF64 program header */
typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;
    Elf64_Addr  p_vaddr;
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} __attribute__((packed)) Elf64_Phdr;

/* Load result */
typedef struct {
    uint64_t entry;       /* Entry point virtual address */
    uint64_t load_base;   /* Lowest vaddr loaded */
    uint64_t load_end;    /* Highest vaddr loaded */
} elf_load_result_t;

/* Load an ELF binary from memory into user address space.
   Returns 0 on success, -1 on error. */
int elf_load(address_space_t* as, const void* elf_data, uint64_t elf_size, elf_load_result_t* result);

#endif
