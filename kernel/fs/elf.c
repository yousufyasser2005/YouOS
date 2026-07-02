#include <kernel/elf.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>
#include <stdint.h>

static void memcpy8(uint8_t* dst, const uint8_t* src, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) dst[i] = src[i];
}
static void memzero(uint8_t* dst, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) dst[i] = 0;
}

/* Load ELF into the CURRENT address space (whatever CR3 points to).
   Caller must have already switched to the target address space. */
int elf_load(address_space_t* as, const void* elf_data, uint64_t elf_size, elf_load_result_t* result) {
    const uint8_t* data = (const uint8_t*)elf_data;
    if (elf_size < sizeof(Elf64_Ehdr)) { vga_puts_color("  [ELF] Too small\n", VGA_LIGHT_RED, VGA_BLACK); return -1; }

    const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)data;
    if (ehdr->e_ident[0] != ELF_MAGIC0 || ehdr->e_ident[1] != ELF_MAGIC1 ||
        ehdr->e_ident[2] != ELF_MAGIC2 || ehdr->e_ident[3] != ELF_MAGIC3) {
        vga_puts_color("  [ELF] Bad magic\n", VGA_LIGHT_RED, VGA_BLACK); return -1;
    }
    if (ehdr->e_ident[4] != ELFCLASS64) { vga_puts_color("  [ELF] Not 64-bit\n", VGA_LIGHT_RED, VGA_BLACK); return -1; }
    if (ehdr->e_type != ET_EXEC)        { vga_puts_color("  [ELF] Not executable\n", VGA_LIGHT_RED, VGA_BLACK); return -1; }

    result->entry     = ehdr->e_entry;
    result->load_base = (uint64_t)-1;
    result->load_end  = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr* phdr = (const Elf64_Phdr*)(data + ehdr->e_phoff + i * ehdr->e_phentsize);
        if (phdr->p_type != PT_LOAD || phdr->p_memsz == 0) continue;

        uint64_t vaddr  = phdr->p_vaddr;
        uint64_t memsz  = phdr->p_memsz;
        uint64_t filesz = phdr->p_filesz;
        uint64_t offset = phdr->p_offset;

        if (vaddr < result->load_base) result->load_base = vaddr;
        if (vaddr + memsz > result->load_end) result->load_end = vaddr + memsz;

        uint64_t pte_flags = PTE_PRESENT | PTE_USER;
        if (phdr->p_flags & PF_W) pte_flags |= PTE_WRITABLE;

        uint64_t page_start = vaddr & ~(uint64_t)0xFFF;
        uint64_t page_end   = (vaddr + memsz + 0xFFF) & ~(uint64_t)0xFFF;

        for (uint64_t va = page_start; va < page_end; va += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) { vga_puts_color("  [ELF] OOM\n", VGA_LIGHT_RED, VGA_BLACK); return -1; }
            memzero((uint8_t*)phys, PAGE_SIZE);

            uint64_t copy_start = (vaddr > va)        ? vaddr        : va;
            uint64_t copy_end   = (vaddr+filesz < va+PAGE_SIZE) ? vaddr+filesz : va+PAGE_SIZE;
            if (copy_start < copy_end) {
                memcpy8((uint8_t*)phys + (copy_start - va),
                        data + offset + (copy_start - vaddr),
                        copy_end - copy_start);
            }
            vmm_map(as, va, phys, pte_flags);
        }
    }

    if (result->load_base == (uint64_t)-1) {
        vga_puts_color("  [ELF] No loadable segments\n", VGA_LIGHT_RED, VGA_BLACK); return -1;
    }

    return 0;
}
