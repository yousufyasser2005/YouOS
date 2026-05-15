#include <stdint.h>
#include <kernel/vga.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/pic.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/heap.h>
#include <kernel/terminal.h>
#include <kernel/process.h>
#include <kernel/syscall.h>
#include <kernel/userspace.h>
#include <kernel/initrd.h>
#include <kernel/elf.h>

#define MULTIBOOT2_MAGIC 0x36D76289

static int kstrcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static void print_uint64(uint64_t val) {
    char buf[21]; int i = 20; buf[i] = '\0';
    if (!val) { vga_puts("0"); return; }
    while (val) { buf[--i] = '0' + (val % 10); val /= 10; }
    vga_puts(&buf[i]);
}

void kernel_main(uint32_t mb2_magic, uint32_t mb2_info) {
    vga_init();
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts_color("                             Welcome to YouOS                                  \n", VGA_YELLOW, VGA_BLACK);
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");

    if (mb2_magic == MULTIBOOT2_MAGIC) {
        vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
        vga_puts("Multiboot2 bootloader detected\n");
    } else {
        vga_puts_color("  [!!] ", VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("WARNING: Invalid Multiboot2 magic!\n");
    }
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("VGA driver initialized\n");

    gdt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("GDT loaded\n");

    idt_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("IDT loaded\n");

    irq_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("PIC initialized\n");

    pmm_init((uint64_t)mb2_info);
    pmm_stats_t stats = pmm_get_stats();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("PMM initialized — ");
    print_uint64(stats.free_pages / 256);
    vga_puts(" MB free\n");

    vmm_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("VMM initialized\n");

    heap_init();
    initrd_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Heap initialized\n");

    terminal_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Keyboard driver loaded\n");

    scheduler_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Scheduler initialized\n");

    pic_unmask(IRQ_KEYBOARD);

    syscall_init();
    vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Syscall interface initialized (SYSCALL/SYSRET)\n");

    vga_puts("\n");
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts_color("  YouOS shell — type 'help' for commands\n", VGA_WHITE, VGA_BLACK);
    vga_puts_color("================================================================================\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");

    char line[256];
    while (1) {
        vga_puts_color("YouOS> ", VGA_LIGHT_GREEN, VGA_BLACK);
        terminal_readline(line, sizeof(line));

        if (kstrcmp(line, "help") == 0) {
            vga_puts_color("  Commands:\n", VGA_LIGHT_CYAN, VGA_BLACK);
            vga_puts("    help      - show this message\n");
            vga_puts("    clear     - clear the screen\n");
            vga_puts("    mem       - show memory stats\n");
            vga_puts("    heap      - show heap stats\n");
            vga_puts("    ps        - list processes\n");
            vga_puts("    version   - show YouOS version\n");
            vga_puts("    userspace - run Ring 3 program\n");
            vga_puts("    exec <name> - run ELF from initrd\n");

        } else if (kstrcmp(line, "clear") == 0) {
            vga_clear();

        } else if (kstrcmp(line, "mem") == 0) {
            pmm_stats_t s = pmm_get_stats();
            vga_puts_color("  Memory:\n", VGA_LIGHT_CYAN, VGA_BLACK);
            vga_puts("    Free  : "); print_uint64(s.free_pages/256); vga_puts(" MB\n");
            vga_puts("    Used  : "); print_uint64(s.used_pages/256); vga_puts(" MB\n");
            vga_puts("    Total : "); print_uint64(s.total_pages/256); vga_puts(" MB\n");

        } else if (kstrcmp(line, "heap") == 0) {
            heap_dump_stats();

        } else if (kstrcmp(line, "ps") == 0) {
            vga_puts_color("  PID  STATE    NAME\n", VGA_LIGHT_CYAN, VGA_BLACK);
            for (uint32_t pid = 0; pid <= 4; pid++) {
                process_t* p = process_get(pid);
                if (!p) continue;
                vga_puts("  "); print_uint64(p->pid);
                vga_puts("    ");
                const char* states[] = {"READY  ","RUNNING","SLEEP  ","DEAD   "};
                vga_puts(states[p->state]);
                vga_puts("  "); vga_puts(p->name); vga_puts("\n");
            }

        } else if (kstrcmp(line, "version") == 0) {
            vga_puts_color("  YouOS v0.1.0\n", VGA_YELLOW, VGA_BLACK);
            vga_puts("  Architecture : x86_64\n");
            vga_puts("  Built from scratch — no Linux\n");

        } else if (line[0]=='e' && line[1]=='x' && line[2]=='e' && line[3]=='c' && line[4]==' ') {
            const char* name = line + 5;
            uint64_t elf_size = 0;
            const void* elf_data = initrd_find(name, &elf_size);
            if (!elf_data) {
                vga_puts_color("  [!!] Not found: ", VGA_LIGHT_RED, VGA_BLACK);
                vga_puts(name); vga_puts("\n");
            } else {
                elf_load_result_t res;
                if (elf_load(elf_data, elf_size, &res) == 0) {
                    extern void tss_set_kernel_stack(uint64_t);
                    static uint8_t elf_kstack[8192];
                    tss_set_kernel_stack((uint64_t)elf_kstack + sizeof(elf_kstack));
                    uint64_t stack_base = pmm_alloc_pages(4);
                    uint64_t stack_top  = stack_base + 4 * PAGE_SIZE;
                    for (uint64_t a = stack_base; a < stack_top; a += 4096)
                        vmm_map(&kernel_as, a, a, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
                    /* Flush TLB */
                    __asm__ volatile("mov %%cr3,%%rax; mov %%rax,%%cr3":::"rax","memory");
                    vga_puts_color("  [OK] Jumping to ELF entry...\n", VGA_LIGHT_GREEN, VGA_BLACK);
                    extern void jump_to_userspace(uint64_t entry, uint64_t stack);
                    jump_to_userspace(res.entry, stack_top);
                }
            }
        } else if (kstrcmp(line, "userspace") == 0) {
            extern void hello_main(void);
            extern void tss_set_kernel_stack(uint64_t stack);
            static uint8_t ring3_kstack[8192];
            tss_set_kernel_stack((uint64_t)ring3_kstack + sizeof(ring3_kstack));
            vga_puts_color("  Creating Ring 3 process...\n", VGA_YELLOW, VGA_BLACK);
            user_process_t* proc = user_process_create("hello", hello_main);
            if (!proc) {
                vga_puts_color("  [!!] Failed\n", VGA_LIGHT_RED, VGA_BLACK);
            } else {
                vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
                vga_puts("Jumping to Ring 3...\n");
                vga_puts_color("  ----------------------------------------\n", VGA_DARK_GREY, VGA_BLACK);
                user_process_exec(proc);
                vga_puts_color("  ----------------------------------------\n", VGA_DARK_GREY, VGA_BLACK);
                vga_puts_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
                vga_puts("Returned from Ring 3\n");
                user_process_destroy(proc);
            }

        } else if (line[0] != '\0') {
            vga_puts_color("  Unknown command: ", VGA_LIGHT_RED, VGA_BLACK);
            vga_puts(line);
            vga_puts("\n  Type 'help' for available commands.\n");
        }
    }
}
